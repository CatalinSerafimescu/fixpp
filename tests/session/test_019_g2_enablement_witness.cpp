// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_019_g2_enablement_witness.cpp
//
// 019-app-callbacks T020 — G2 enablement witness.
//
// Proves SC-002: an opaque application-message round-trip
// (NewOrderSingle 35=D → ExecutionReport 35=8) is drivable through the
// PUBLIC 019 surface — `co_await engine.send(id, payload)` for origination
// and `fromApp(…)` for observation — without typed business-message schemas.
//
// Harness: tests/session/ live loopback pattern (mirrors engine_lifecycle_test).
// ONE engine, ONE acceptor + ONE initiator session, connected over real loopback
// TLS on a pre-reserved port. GTEST_SKIP if FIXPP_TLS_FIXTURE_DIR absent.
//
// Chain:
//   1. engine.send(initiator_id, nos_payload)
//        → acceptor's fromApp fires with MsgType "D"
//   2. engine.send(acceptor_id, er_payload)
//        → initiator's fromApp fires with MsgType "8"
//
// Both observations use the SAME WitnessApplication; the SessionId parameter
// passed to fromApp identifies which side observed the message.
//
// Self-deadline: all waits use run_for() with a bounded wall-clock loop.
// [[feedback_fail_placeholder_red_test]]
//
// Anchors: spec.md SC-002, US1/US2; FR-003/006; tasks.md T020;
//          engine_lifecycle_test.cpp (loopback-TLS + port-reservation pattern).

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────
//
// ⚠️ THIS FILE'S TWO CENSUS SITES ADOPT `pump_until_ready`, NOT
// `run_window_then_ready`, AND THE REASON IS THE SHAPE OF THE WAIT, NOT A
// PREFERENCE. Each site drives a HAND-ROLLED bounded loop whose predicate is the
// application callback count, NOT the send's own future; the `get()` that follows is
// therefore unconditional against a future nothing waited on. Wrapping it in
// `run_window_then_ready` would be wrong twice over: that primitive pumps for the
// whole window BEFORE testing readiness, so a window wide enough to be safe would be
// paid in full on every passing run HERE. ⚠️ That is a property of THIS SITE, not of
// `run_window_then_ready`: `run_for` returns early once the context runs out of work, and
// the primitive's own doc says so ("no work guard ... the window costs what it costs
// today"). It holds here because a live loopback-TLS engine always has outstanding work --
// the listener's accept, each session's read pump, the detached liveness loop -- so the
// context never empties and the window always runs to its end.
// `pump_until_ready` tests readiness FIRST and
// returns immediately when the loop above already satisfied it, so the happy path
// costs nothing and a lost wake FAILS at the budget instead of hanging.
// It reports with `kPumpBudgetMiss` (a budget was granted and exhausted) rather than
// `kWindowMiss` (a preserved window closed).
//
// ⚠️ CONSEQUENCE FOR VERIFICATION: THE SEAM REACHES BOTH SITES, because both calls pass
// a label; `ci/pump-red-arm.sh` still cannot rewrite them, so the seam is the only driver
// and `ci/red-arms/batch13-labels.txt` carries both arms.
//
// ⚠️ THESE TWO SITES ARE WHY THE SEAM WAS EXTENDED TO THIS PRIMITIVE AT ALL, and the
// measurement below is that argument -- keep it. Before the extension they were mutated
// BY HAND, and the obvious hand recipe PRODUCES A CLEAN PASS THAT READS EXACTLY LIKE A
// BROKEN MISS BRANCH. `pump_until` evaluates `ready()`
// BEFORE it pumps, so a zero budget still returns TRUE at a site whose future is already
// ready at entry -- which is the normal state here, because the loop above has just
// finished driving the send. Measured: budget 0s alone left this test PASSING, with
// `[ RUN ]`/`[ OK ]` in the output, so it was not a skip.
//   The forcing mutation must ALSO starve the loop above (`now() + 3s` -> `now()`), which
//   is what makes the future unready and puts the site in the state the guard exists for.
//   Force ONE site at a time: the miss branch returns, so a mutation applied to both
//   covers only the first.
// This is the same asymmetry that settled `run_window_then_ready`'s seam on "announce,
// do not dispatch, RETURN FALSE" rather than on shrinking durations -- shrinking cannot
// force a wait that already completed. It applies to budget sites too.
//
// The drain is the CLOCKED one, spelled `*engine.clock()`. `engine` is a
// `fixpp::session::Engine`, so the ACCESSOR exists; the `EngineConfig` that carried
// the clock was `std::move`d into it at construction, so the config's own `clock`
// member is a moved-from `shared_ptr` by the time a miss branch could read it.
// Non-nullness rests on the assignment in this test body -- `ecfg.clock` is set two
// statements above the engine's construction -- and NOT on the accessor's
// "never null post-construction" comment, which is #289's standing known-false one.

using namespace std::chrono_literals;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace {

// ── Helpers ───────────────────────────────────────────────────────────────────

const char* get_fixture_dir() {
    const char* env = std::getenv("FIXPP_TLS_FIXTURE_DIR");  // NOLINT(concurrency-mt-unsafe)
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    return (env && env[0] != '\0') ? env : kDir;
}

// Reserve a free loopback port so we can register the acceptor + initiator with
// the same port before start() (mirrors engine_lifecycle_test.cpp).
static uint16_t reserve_free_port(asio::io_context& ioc) {
    asio::ip::tcp::acceptor a{ioc};
    asio::ip::tcp::endpoint ep{asio::ip::make_address("127.0.0.1"), 0};
    a.open(ep.protocol());
    a.bind(ep);
    uint16_t port = a.local_endpoint().port();
    a.close();
    return port;
}

// ── WitnessApplication ────────────────────────────────────────────────────────
// Tracks each (SessionId, MsgType) pair observed via fromApp.
// Thread-safe: fromApp fires on the session strand; mutex-protected records.

struct FromAppRecord {
    SessionId session_id;
    std::string msg_type;
};

class WitnessApplication : public Application {
public:
    mutable std::mutex mu;
    std::vector<FromAppRecord> records;

    expected_t<void> fromApp(const MessageView<access_mode::Index>& msg,
                             const SessionId& id) override {
        auto fv = msg.get(35);
        std::string mt = fv ? std::string(fv->as_string()) : "<none>";
        {
            std::lock_guard<std::mutex> lk(mu);
            records.push_back({id, std::move(mt)});
        }
        return {};
    }

    int count_for(const SessionId& id) const {
        std::lock_guard<std::mutex> lk(mu);
        int n = 0;
        for (const auto& r : records)
            if (r.session_id == id) ++n;
        return n;
    }

    std::string last_msg_type_for(const SessionId& id) const {
        std::lock_guard<std::mutex> lk(mu);
        for (auto it = records.rbegin(); it != records.rend(); ++it)
            if (it->session_id == id) return it->msg_type;
        return {};
    }
};

// ── Opaque payload builders ───────────────────────────────────────────────────

static std::vector<std::byte> make_nos_payload() {
    // NewOrderSingle body fields. MsgType (35=D) MUST be included in the
    // payload so the receiver's frame-scanner extracts it for fromApp dispatch.
    // Session::send_impl writes 8=/9=/34=/49=/52=/56= then appends app_payload;
    // it does NOT stamp 35=. The payload must carry it. [send_impl lines 2775-2822]
    static const char k[] =
        "35=D\x01""11=ORD001\x01""54=1\x01""55=AAPL\x01""40=2\x01""44=100.0\x01";
    std::vector<std::byte> v;
    for (const char* p = k; *p; ++p) v.push_back(static_cast<std::byte>(*p));
    return v;
}

static std::vector<std::byte> make_exec_report_payload() {
    // ExecutionReport body fields. MsgType (35=8) included for the same reason.
    static const char k[] =
        "35=8\x01""17=EXEC001\x01""37=ORD001\x01""39=2\x01""150=2\x01""151=0\x01";
    std::vector<std::byte> v;
    for (const char* p = k; *p; ++p) v.push_back(static_cast<std::byte>(*p));
    return v;
}

// ── Test ──────────────────────────────────────────────────────────────────────

TEST(G2EnablementWitness, OpaqueRoundTripViaEngineLoopback) {
    const char* fixture_dir = get_fixture_dir();
    if (!fixture_dir || fixture_dir[0] == '\0')
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set — skipping live loopback witness";

    asio::io_context ioc;

    // ── Build TLS factory (mTLS-CA, mirrors engine_lifecycle_test) ───────────
    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    ASSERT_TRUE(cs_r.has_value()) << "cert_source build failed";

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    ASSERT_TRUE(fac_r.has_value()) << "transport factory build failed";
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    // ── Reserve a port so both sessions can be registered with the same port ──
    const uint16_t port = reserve_free_port(ioc);

    // ── Build the application and engine ─────────────────────────────────────
    auto app = std::make_shared<WitnessApplication>();

    fixpp::core::EngineConfig ecfg;
    ecfg.executor = ioc.get_executor();
    ecfg.application = app;
    // 041 T019: Engine::start() rejects a null clock with clock_not_set.
    ecfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    fixpp::session::Engine engine{ioc.get_executor(), std::move(ecfg)};

    // ── Register acceptor + initiator ─────────────────────────────────────────
    // The leaf cert CN is "fixpp-leaf-rsa2048" (loopback fixture convention).
    auto make_cfg = [&](const char* sender, const char* target,
                        fixpp::session::session_role role, const char* peer_compid) {
        fixpp::session::SessionConfig c;
        c.sender_comp_id = sender;
        c.target_comp_id = target;
        c.begin_string = "FIX.4.2";
        c.role = role;
        c.executor_override = ioc.get_executor();
        c.security_profile =
            fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
        c.compid_authorization_policy.add_binding("fixpp-leaf-rsa2048", peer_compid);
        c.dictionary = fixpp::test_support::make_minimal_dictionary();
        c.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        c.transport_factory_override = fac;
        c.heartbeat_interval = std::chrono::seconds{30};
        c.logout_disconnect_timeout_ms = 2000;
        c.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", port};
        c.transport_send = [](std::span<const std::byte>) {};  // rebound on attach
        return c;
    };

    auto acc_cfg = make_cfg("ACCEPTOR", "INITIATOR",
                            fixpp::session::session_role::acceptor, "INITIATOR");
    auto ini_cfg = make_cfg("INITIATOR", "ACCEPTOR",
                            fixpp::session::session_role::initiator, "ACCEPTOR");
    const auto acc_id = SessionId::from_config(acc_cfg);
    const auto ini_id = SessionId::from_config(ini_cfg);

    ASSERT_TRUE(engine.register_session(std::move(acc_cfg)).has_value())
        << "register acceptor failed";
    ASSERT_TRUE(engine.register_session(std::move(ini_cfg)).has_value())
        << "register initiator failed";

    // ── Start engine and wait for both sessions to reach Active ──────────────
    ASSERT_TRUE(engine.start().has_value()) << "engine.start() failed";

    bool acc_active = false, ini_active = false;
    auto deadline_logon = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline_logon &&
           (!acc_active || !ini_active)) {
        ioc.run_for(100ms);
        ioc.restart();
        auto acc_s = engine.lookup(acc_id);
        auto ini_s = engine.lookup(ini_id);
        acc_active = acc_s && acc_s->state() == fixpp::session::fsm_state::Active;
        ini_active = ini_s && ini_s->state() == fixpp::session::fsm_state::Active;
    }

    ASSERT_TRUE(acc_active) << "Acceptor did not reach Active within 5s";
    ASSERT_TRUE(ini_active) << "Initiator did not reach Active within 5s";

    // ── Step 1: initiator sends NOS (35=D) via engine.send ───────────────────
    // fromApp on the ACCEPTOR session must fire with MsgType "D".
    {
        auto nos = make_nos_payload();
        auto send_fut = asio::co_spawn(
            ioc, engine.send(ini_id, std::span<const std::byte>(nos)), asio::use_future);

        // Drive until acceptor's fromApp fires with "D" (bounded 3s).
        auto dl = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < dl && app->count_for(acc_id) == 0) {
            ioc.run_for(100ms);
            ioc.restart();
        }

        // The loop above waits on the APPLICATION callback, not on `send_fut`; this
        // is what makes the `get()` conditional. Ready-first, so a passing run pays
        // nothing here.
        if (!fixpp::test_support::pump_until_ready(ioc, send_fut, 3s,
                                                   "G2EnablementWitness/send_nos")) {
            // ⚠️ STOP THE ENGINE BEFORE DRAINING. THIS IS SURVIVOR CASE (ii) AT THE
            // PRIMITIVE, AND BATCH 12 RECORDED THAT NO #289 SITE REACHED IT -- THIS IS THE
            // FIRST THAT DOES.
            //
            // WHAT WAS MEASURED, stated separately from what it is attributed to, because
            // the two were run together in one sentence and only the first is established:
            // with this `engine.stop()` deleted and the drain left alone, a forced miss at
            // this site emits `kDrainResidual`. That was first seen under a HAND mutation
            // and re-measured under the SEAM while adding the driver's residual check --
            // delete the stop, rebuild `g2_enablement_witness_019_test`, force both g2
            // labels, and the sweep reports `RED=1 ... RESIDUAL=1 of 2`, this site demoted
            // and its sibling still clean. So the drain alone does NOT quiesce here.
            //
            // WHAT IS NOT ESTABLISHED: which outstanding work is responsible. A live engine
            // holds several things at once -- the listener's accept, each session's read
            // pump, a detached `run_liveness_loop` -- and the awaited send may or may not
            // still be parked in `async_write` depending on how far the loop above got.
            // `kDrainResidual`'s text names the transport case, but that text is a general
            // HINT printed on every residual, not a diagnosis of this one. An earlier
            // revision of this comment read the hint as the finding.
            // It does not matter for the fix: `engine.stop()` retires all of them, which is
            // why the remedy is right whichever it was. It matters for anyone reasoning
            // FROM this comment, which is why the distinction is written down.
            // `drain_or_report`'s comment lists three answers; the single-`Transport*`
            // overload is not one of them here, because the engine owns two sessions plus a
            // listener and that parameter takes one. The remaining answer is "close the
            // transport yourself first", and at engine level that spells `engine.stop()` --
            // the same call this test's own teardown block already makes.
            // Bounded, and its verdict DELIBERATELY ignored: the drain that follows is what
            // reports whatever `stop()` failed to release, so swallowing a stop() miss here
            // cannot hide a residual.
            auto quiesce_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
            (void)fixpp::test_support::pump_until_ready(ioc, quiesce_fut, 5s);
            fixpp::test_support::cancel_and_drain_or_report(ioc, *engine.clock(),
                                                            "G2EnablementWitness/send_nos");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "G2EnablementWitness/send_nos";
            return;
        }
        auto result = send_fut.get();
        ASSERT_TRUE(result.has_value())
            << "engine.send(NOS) failed: error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));

        ASSERT_GE(app->count_for(acc_id), 1)
            << "Acceptor: fromApp did not fire after NOS send (MsgType 'D')";
        EXPECT_EQ(app->last_msg_type_for(acc_id), "D")
            << "Acceptor: expected fromApp MsgType 'D' (NOS) but got '"
            << app->last_msg_type_for(acc_id) << "'";
    }

    // ── Step 2: acceptor sends ExecutionReport (35=8) via engine.send ────────
    // fromApp on the INITIATOR session must fire with MsgType "8".
    {
        auto er = make_exec_report_payload();
        auto send_fut = asio::co_spawn(
            ioc, engine.send(acc_id, std::span<const std::byte>(er)), asio::use_future);

        // Drive until initiator's fromApp fires with "8" (bounded 3s).
        auto dl = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < dl && app->count_for(ini_id) == 0) {
            ioc.run_for(100ms);
            ioc.restart();
        }

        // The loop above waits on the APPLICATION callback, not on `send_fut`; this
        // is what makes the `get()` conditional. Ready-first, so a passing run pays
        // nothing here.
        if (!fixpp::test_support::pump_until_ready(ioc, send_fut, 3s,
                                                   "G2EnablementWitness/send_er")) {
            // Same shape as the NOS site above, for the same measured reason (survivor
            // case (ii): a send parked on a live TLS transport). The reasoning is written
            // once, there -- not copied, so it cannot drift into being false at one site.
            auto quiesce_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
            (void)fixpp::test_support::pump_until_ready(ioc, quiesce_fut, 5s);
            fixpp::test_support::cancel_and_drain_or_report(ioc, *engine.clock(),
                                                            "G2EnablementWitness/send_er");
            ADD_FAILURE() << fixpp::test_support::kPumpBudgetMiss << "G2EnablementWitness/send_er";
            return;
        }
        auto result = send_fut.get();
        ASSERT_TRUE(result.has_value())
            << "engine.send(ER) failed: error="
            << (result.has_value() ? 0 : static_cast<int>(result.error()));

        ASSERT_GE(app->count_for(ini_id), 1)
            << "Initiator: fromApp did not fire after ExecutionReport send (MsgType '8')";
        EXPECT_EQ(app->last_msg_type_for(ini_id), "8")
            << "Initiator: expected fromApp MsgType '8' (ER) but got '"
            << app->last_msg_type_for(ini_id) << "'";
    }

    // ── Teardown ──────────────────────────────────────────────────────────────
    {
        auto stop_fut = asio::co_spawn(ioc, engine.stop(), asio::use_future);
        ioc.run();
        stop_fut.get();
    }
    EXPECT_TRUE(engine.stopped()) << "engine must be stopped() after stop()";
}

}  // namespace
