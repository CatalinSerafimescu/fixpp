// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 fixpp contributors
//
// tests/session/engine_readpump_test.cpp — T014 [RED] [US2] Phase 4
//
// TDD RED: read-pump delivers subsequent frames to on_inbound_frame.
//
// Anchors: tasks.md T014; spec.md US2 AC1/AC2; SC-003; FR-004/012; C2.
//
// Cases:
//   1. InOrderExactlyOnce (LOAD-BEARING RED)
//      Logon + N≥2 Heartbeats in seq order → assert next_inbound_unsafe()==2+N.
//      RED today: run_read_pump_stub co_returns immediately → no subsequent
//      frames delivered → counter stays at 2. Expected 2+N=4, actual 2.
//
//   2. OverCapacityFrameClosesSession
//      After logon, client sends a single frame clearly exceeding any reasonable
//      read-pump carry limit (>64 KiB of padding). Expectation: session ends up
//      in Disconnected or next_inbound stays at 2 (stub never reacts either way,
//      so this case PASSES trivially today — it is a GREEN-target assertion for
//      T015).  Comment documents what GREEN will look like.
//
//   3. EofDisconnectsSession
//      After logon, client closes TCP connection. Expectation: session reaches
//      Disconnected within the run_for bound (disconnect handling via existing
//      session teardown path — no new disposition). Like case 2, this may pass
//      trivially on the stub (no pump = no keepalive either).  Documented
//      GREEN-target; the load-bearing RED witness is case 1.
//
//   6-8. FramerGarbleDisregardedInEstablishedSession_* (093 FR-001, contract C-1;
//      they superseded 092's FramerFailureClosesEstablishedSession_* pins, and
//      L-004-4 closes): a frame the Framer finds garbled (bad CheckSum, too-small
//      BodyLength, malformed BeginString prefix) is disregarded, and the
//      established session carries on. See the section above the cells.
//
// Anti-hang: every coroutine carries a self-deadline steady_timer.
//            All ioc.run_for() calls are explicitly bounded.
//
// Observability:
//   SeqnumManager::next_inbound_unsafe() (via session_test_access::seqnum_mgr).
//   Session::state() for terminal-state cases.
//
// Drive path: acceptor loopback (US1 wired: accept→handshake→resolve→attach→
//   admit). Mirrors engine_acceptor_test.cpp + engine_firstframe_test.cpp.

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/core/system_clock_source.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/seqnum_manager.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/file_cert_source.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <fixpp/wire/framer.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "engine_loopback_harness.hpp"
#include "plain_engine_rig.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────
//
// This file's one census site uses `run_window_then_ready` plus a miss-branch drain
// (tests/support/pump_until_ready.hpp). The window is PRESERVED: the hazard #289
// names is the UNCONDITIONAL `get()`, not the fixed window.
//
// ⚠️ THIS SITE IS A NORMALISATION, NOT A HAZARD FIX, and saying so is the point.
// An `ASSERT_EQ(close_fut.wait_for(0s), ready)` already stood between the window and
// the `get()`. What the migration buys is the shared report text and the FORCING
// SEAM, not a removed deadlock. The census flags it because the census is LEXICAL and
// cannot see an assertion standing between the two lines it matches.
//
// The site label passed to `run_window_then_ready` is that seam: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// ⚠️ THE OTHER `.get()`s IN THIS FILE ARE A DIFFERENT SHAPE, AND #289 BATCH 17 MIGRATED
// THEM -- this paragraph used to end "and are DELIBERATELY NOT MIGRATED", which stopped
// being true. They are `ioc.run(); stop_fut.get();`, an UNBOUNDED run rather than a
// bounded window, so `ci/pump-census.sh` still does not scan them and
// `run_window_then_ready` is still the wrong primitive for them. They now call
// `run_to_exhaustion_or_report`, which preserves the unbounded `run()` and only asks
// whether the future came back ready. Bounding `run()` itself remains a separate and
// open question. Derive the population with `bash ci/pump-get-sweep.sh --disposition`
// rather than from a count written here.
//
// ⚠️ SOME OF THIS FILE'S NEW SITES SIT IN EARLY-EXIT GUARD BRANCHES that do not execute
// when the acceptance path is live, so `ci/pump-seam-arm.sh` reports them NO-SUCH-SITE.
// That is the driver being honest about an unreached site, not a defect -- and it is a
// reason to READ a NO-SUCH-SITE before believing it names a missing label. ⚠️ The branches
// are not all the same shape: some are `if (!acc)` (session not found / already freed) and
// at least one is a state guard (`st == NotConnected || st == LogonSent`). An earlier
// revision of this paragraph said all of them were `if (!acc)`, which is wrong and would
// send the next reader looking for the wrong thing. Derive the set instead:
//   bash ci/pump-seam-arm.sh linux-clang-debug ci/red-arms/batch17-labels.txt
// and read each NO-SUCH-SITE label's own enclosing guard.
//
// The drain is the CLOCKED one, spelled `*h->engine->clock()`. `build_harness` above
// installs a real `system_clock_source` into the `EngineConfig` and `std::move`s that
// config into the engine, so the accessor is the only live spelling; non-nullness
// rests on that assignment, not on the accessor's "never null post-construction"
// comment, which is #289's standing known-false one.
#include "support/session_test_access.hpp"
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;
using fixpp::session::fsm_state;

namespace {

// ── Frame builder helpers ─────────────────────────────────────────────────────

// 041 T019: a real engine clock activates the session SendingTime(52) MaxLatency
// guard (inert under the prior null clock). Inbound frames feeding a live session
// must carry a fresh 52 or the guard rejects them (reason=10). Mirrors the 038/US3
// fix in engine_acceptor_test.cpp.
std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

std::vector<std::byte> make_fix_frame(std::string_view begin_str, std::string_view msg_type,
                                      int seq_num, std::string_view sender, std::string_view target,
                                      std::string extra_body = "") {
    auto field = [](int tag, std::string_view v) -> std::string {
        return std::to_string(tag) + "=" + std::string(v) + "\x01";
    };
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq_num));
    body += field(49, sender);
    body += field(56, target);
    body += field(52, utc_now_fix_timestamp());  // SendingTime (fresh; 041 T019 clock gate)
    body += extra_body;

    std::string msg;
    msg += "8=" + std::string(begin_str) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

std::vector<std::byte> make_logon_frame(std::string_view begin_str, std::string_view sender,
                                        std::string_view target) {
    return make_fix_frame(begin_str, "A", 1, sender, target,
                          "98=0\x01"
                          "108=30\x01");
}

std::vector<std::byte> make_heartbeat_frame(std::string_view begin_str, int seq_num,
                                            std::string_view sender, std::string_view target) {
    return make_fix_frame(begin_str, "0", seq_num, sender, target);
}

// ── Engine + session setup helper ────────────────────────────────────────────
// Returns (Engine unique_ptr, acceptor SessionId, loopback fixture) ready to
// start.  Mirrors engine_acceptor_test.cpp's OnListIdentityAdmitsToEstablished
// setup.  Returns nullptr if the fixture directory is absent.

struct ReadPumpHarness {
    std::unique_ptr<fixpp::session::Engine> engine;
    fixpp::session::SessionId acc_id;
    std::unique_ptr<fixpp::transport::test::LoopbackTlsFixture> fixture;
};

std::unique_ptr<ReadPumpHarness> build_harness(asio::io_context& ioc) {
    const char* dir = std::getenv("FIXPP_TLS_FIXTURE_DIR");
#ifdef FIXPP_TLS_FIXTURE_DIR
    static const char* kDir = FIXPP_TLS_FIXTURE_DIR;
#else
    static const char* kDir = nullptr;
#endif
    const char* fixture_dir = dir ? dir : kDir;
    if (!fixture_dir || fixture_dir[0] == '\0') return nullptr;

    fixpp::tls::file_cert_source::Config cs_cfg;
    cs_cfg.leaf_path = std::string(fixture_dir) + "/leaf_rsa2048.pem";
    cs_cfg.private_key_path = std::string(fixture_dir) + "/leaf_rsa2048.key";
    cs_cfg.ca_bundle_path = std::string(fixture_dir) + "/ca.pem";
    auto cs_r = fixpp::tls::file_cert_source::make_file_cert_source(
        cs_cfg, std::pmr::new_delete_resource());
    if (!cs_r.has_value()) return nullptr;

    fixpp::tls::SslCtxConfig ssl;
    ssl.profile = fixpp::tls::SecurityProfile::mtls_ca;
    ssl.cs = std::move(*cs_r);
    ssl.clock = nullptr;
    ssl.caps = fixpp::tls::CertSourceCaps{};

    auto fac_r = fixpp::transport::make_asio_tls_transport_factory(
        fixpp::transport::Transport::Config{}, ssl);
    if (!fac_r.has_value()) return nullptr;
    std::shared_ptr<fixpp::transport::TransportFactory> fac{std::move(*fac_r)};

    fixpp::session::CompIdAuthorizationPolicy authz;
    authz.add_binding("fixpp-leaf-rsa2048", "INITIATOR");

    fixpp::core::EngineConfig eng_cfg;
    eng_cfg.executor = ioc.get_executor();
    // 041 US3: Engine::start() now gates on a non-null clock (clock_not_set);
    // this harness previously relied on start() being void. Provide a real clock.
    eng_cfg.clock = std::make_shared<fixpp::core::system_clock_source>(ioc.get_executor());

    auto h = std::make_unique<ReadPumpHarness>();

    h->engine = std::make_unique<fixpp::session::Engine>(ioc.get_executor(), std::move(eng_cfg));

    fixpp::session::SessionConfig acc;
    acc.sender_comp_id = "ACCEPTOR";
    acc.target_comp_id = "INITIATOR";
    acc.begin_string = "FIX.4.2";
    acc.role = fixpp::session::session_role::acceptor;
    acc.executor_override = ioc.get_executor();
    acc.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    acc.compid_authorization_policy = authz;
    acc.dictionary = fixpp::test_support::make_minimal_dictionary();
    acc.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    acc.transport_factory_override = fac;
    acc.heartbeat_interval = std::chrono::seconds{30};
    acc.logout_disconnect_timeout_ms = 2000;
    acc.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 0};
    acc.transport_send = [](std::span<const std::byte>) {};

    h->acc_id = fixpp::session::SessionId::from_config(acc);
    if (!h->engine->register_session(std::move(acc))) return nullptr;

    h->fixture = std::make_unique<fixpp::transport::test::LoopbackTlsFixture>(fixture_dir,
                                                                              ioc.get_executor());

    return h;
}

// ── Standalone mTLS test-initiator coroutine ─────────────────────────────────
// Mirrors run_test_initiator in engine_acceptor_test.cpp.
// Sends a Logon and then the provided `extra_frames` before waiting for
// `wait_after` then closing.  Self-deadline prevents infinite hang on stub.

asio::awaitable<void> run_client_with_extra_frames(
    asio::io_context& ioc, fixpp::transport::test::LoopbackTlsFixture& fixture,
    uint16_t acceptor_port, std::string sender, std::string target,
    std::vector<std::vector<std::byte>> extra_frames,
    std::chrono::milliseconds wait_after = 1500ms) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());

    try {
        auto client = fixture.make_client(ioc.get_executor());
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(client.get());
        if (!tls) co_return;

        fixpp::transport::Endpoint ep{"127.0.0.1", acceptor_port};
        auto conn_r = co_await client->async_connect(ep);
        if (!conn_r.has_value()) co_return;

        auto hs_r = co_await tls->async_handshake(fixture.ssl_cfg());
        if (!hs_r.has_value()) co_return;

        // Send Logon (MsgSeqNum=1).
        auto logon = make_logon_frame("FIX.4.2", sender, target);
        (void)co_await client->async_write(std::span<const std::byte>{logon});

        // Brief pause so the acceptor can process the Logon before we send more.
        asio::steady_timer pause{ioc};
        pause.expires_after(100ms);
        co_await pause.async_wait(asio::use_awaitable);

        // Send extra frames (Heartbeats or oversized payload).
        for (auto const& frame : extra_frames) {
            (void)co_await client->async_write(std::span<const std::byte>{frame});
        }

        // Wait for the server to reply / process, then close.
        asio::steady_timer t{ioc};
        t.expires_after(wait_after);
        co_await t.async_wait(asio::use_awaitable);

        (void)client->close();
    } catch (...) {
    }
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Case 1 — InOrderExactlyOnce  [LOAD-BEARING RED]
//
// Setup:
//   - Acceptor wired with on-list mTLS identity (mirrors engine_acceptor_test).
//   - Client: sends Logon(seq=1), then N=2 Heartbeats(seq=2,3).
//   - Expected after delivery: next_inbound_unsafe() == 2 + N == 4.
//
// RED today:
//   run_read_pump_stub co_returns immediately → the 2 Heartbeats are never fed
//   into on_inbound_frame → SeqnumManager counter stays at 2 (only Logon
//   advanced it from 1→2).
//   EXPECT_EQ(next_inbound, 4) FAILS: actual == 2.
//
// GREEN (T015):
//   real pump reads TCP → parses frames → calls on_inbound_frame per frame →
//   each valid in-sequence Heartbeat advances next_inbound by 1 →
//   next_inbound_unsafe() == 4.
// ─────────────────────────────────────────────────────────────────────────────
TEST(EngineReadPumpTest, InOrderExactlyOnce) {
    asio::io_context ioc;
    auto h = build_harness(ioc);
    if (!h) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(h->engine->start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t port = h->engine->acceptor_bound_endpoint(h->acc_id).port;
    ASSERT_NE(port, 0U) << "acceptor listener did not bind";

    // N = 2 Heartbeats with seqnums 2 and 3, sent as ONE concatenated write so
    // both frames deterministically arrive in a single read_some. This exercises
    // the read-pump's multi-frame drain: feed() emits one frame per `out` slot
    // and retains the surplus in carry; the pump MUST drain the carry before the
    // next read or the 2nd frame is lost on EOF (next_inbound reaches 3, not 4).
    constexpr int N = 2;
    std::vector<std::byte> concat;
    for (int i = 0; i < N; ++i) {
        auto hb = make_heartbeat_frame("FIX.4.2", /*seq=*/2 + i, "INITIATOR", "ACCEPTOR");
        concat.insert(concat.end(), hb.begin(), hb.end());
    }
    std::vector<std::vector<std::byte>> hb_frames;
    hb_frames.push_back(std::move(concat));

    asio::co_spawn(ioc,
                   run_client_with_extra_frames(ioc, *h->fixture, port,
                                                /*sender=*/"INITIATOR", /*target=*/"ACCEPTOR",
                                                std::move(hb_frames),
                                                /*wait_after=*/1500ms),
                   asio::detached);

    // Bound: 4s — the stub co_returns immediately so no hang risk.
    ioc.run_for(4s);
    ioc.restart();

    // Capture BEFORE stop() frees the session.
    auto acc = h->engine->lookup(h->acc_id);

    // If the session never reached established, skip (acceptance path not yet
    // wired for this test environment — but in practice T011/T012/T013 are GREEN
    // so this path is live).
    if (!acc) {
        // stop cleanly then skip
        auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
        if (!fixpp::test_support::run_to_exhaustion_or_report(
                ioc, stop_fut, "EngineReadPumpTest::InOrderExactlyOnce/stop_fut1")) {
            return;
        }
        stop_fut.get();
        GTEST_SKIP() << "acceptor session not found — acceptance path not live";
    }

    // Confirm the session actually reached established state (Logon processed).
    // Accept Active, LogonReceived, AND Disconnected: with the real pump the
    // session may have already processed all N heartbeats AND the client EOF
    // within the 4s window, reaching Disconnected cleanly. That is a valid
    // outcome — next_inbound is still readable from the live Session object
    // (registry_ not cleared until stop()). Only NotConnected / LogonSent
    // indicate the Logon was never processed.
    auto st = acc->state();
    if (st == fsm_state::NotConnected || st == fsm_state::LogonSent) {
        auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
        if (!fixpp::test_support::run_to_exhaustion_or_report(
                ioc, stop_fut, "EngineReadPumpTest::InOrderExactlyOnce/stop_fut2")) {
            return;
        }
        stop_fut.get();
        GTEST_SKIP() << "session never reached established state (state=" << static_cast<int>(st)
                     << ") — Logon accept path not fully wired for this run";
    }

    // ── THE LOAD-BEARING RED WITNESS ─────────────────────────────────────────
    // After Logon (seq=1) is delivered, next_inbound advances 1→2.
    // After each of the N=2 Heartbeats (seq=2,3), it should advance to 4.
    // With the stub, the pump never feeds those frames, so it stays at 2.
    const auto next_inbound = static_cast<int>(
        fixpp::session::session_test_access::seqnum_mgr(*acc).next_inbound_unsafe());
    constexpr int expected = 2 + N;  // 4

    auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "EngineReadPumpTest::InOrderExactlyOnce/stop_fut3")) {
        return;
    }
    stop_fut.get();

    EXPECT_EQ(next_inbound, expected)
        << "SC-003 / US2 AC1: each of the " << N << " in-sequence post-Logon "
        << "Heartbeats must be delivered to on_inbound_frame exactly once, in "
        << "arrival order, advancing next_inbound by 1 per frame. "
        << "Expected next_inbound==" << expected << " but got " << next_inbound << ". "
        << "RED: run_read_pump_stub co_returns immediately — no subsequent frames "
        << "are fed → counter stays at 2. "
        << "GREEN (T015): real pump reads, parses, delivers each frame → counter "
        << "reaches 2+N==" << expected << ".";
}

// ─────────────────────────────────────────────────────────────────────────────
// Case 2 — OverCapacityFrameClosesSession
//
// After logon, client sends a single message whose body is clearly over any
// reasonable carry limit (>64 KiB of padding in the Body field).  The real
// pump detects wire_frame_too_large, calls session.close(terminal), which
// transitions the FSM to Disconnected.
//
// GREEN (T015): pump detects wire_frame_too_large → close(terminal) →
//   session.fsm_state_ == Disconnected.
//
// This assertion FAILS if the pump's wire_frame_too_large arm is deleted:
//   without that arm the pump would keep running (or crash), the session
//   stays in Active/LogonReceived, and state != Disconnected.
// ─────────────────────────────────────────────────────────────────────────────
TEST(EngineReadPumpTest, OverCapacityFrameClosesSession) {
    asio::io_context ioc;
    auto h = build_harness(ioc);
    if (!h) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(h->engine->start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t port = h->engine->acceptor_bound_endpoint(h->acc_id).port;
    ASSERT_NE(port, 0U) << "acceptor listener did not bind";

    // Build an oversized frame: a body of repeated 'X' longer than the session's
    // inbound limit L (no 383 is configured, so L is the default; 093 data-model E-2),
    // so the framer returns wire_frame_too_large before any frame bytes reach
    // on_inbound_frame. The cell checks the body against L once the session exists.
    constexpr std::size_t kOversizeBody = 128 * 1024;  // 128 KiB
    std::vector<std::vector<std::byte>> oversize_frames;
    {
        // Encode as a raw byte sequence with a FIX-shaped header so the
        // transport layer at least ships it; the pump's framer will see the
        // overlong BodyLength and reject it as wire_frame_too_large.
        auto extra = "58=" + std::string(kOversizeBody, 'X') + "\x01";
        oversize_frames.push_back(
            make_fix_frame("FIX.4.2", "0", /*seq=*/2, "INITIATOR", "ACCEPTOR", std::move(extra)));
    }

    asio::co_spawn(ioc,
                   run_client_with_extra_frames(ioc, *h->fixture, port, "INITIATOR", "ACCEPTOR",
                                                std::move(oversize_frames),
                                                /*wait_after=*/1500ms),
                   asio::detached);

    ioc.run_for(4s);
    ioc.restart();

    auto acc = h->engine->lookup(h->acc_id);
    if (!acc) {
        auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
        if (!fixpp::test_support::run_to_exhaustion_or_report(
                ioc, stop_fut, "EngineReadPumpTest::OverCapacityFrameClosesSession/stop_fut1")) {
            return;
        }
        stop_fut.get();
        GTEST_SKIP() << "acceptor session not found";
    }

    auto st = acc->state();
    const auto next_inbound = static_cast<int>(
        fixpp::session::session_test_access::seqnum_mgr(*acc).next_inbound_unsafe());
    std::size_t const limit = fixpp::session::session_test_access::inbound_limit(*acc);

    auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "EngineReadPumpTest::OverCapacityFrameClosesSession/stop_fut2")) {
        return;
    }
    stop_fut.get();

    EXPECT_GT(kOversizeBody, limit) << "the oversized body must exceed the session's L";

    // STRENGTHENED GREEN assertion (T015): pump must have detected the oversized
    // frame and called close(terminal), driving the FSM to Disconnected.
    // Removing the pump's wire_frame_too_large arm leaves state == Active or
    // LogonReceived — this EXPECT_EQ would then FAIL.
    EXPECT_EQ(st, fsm_state::Disconnected)
        << "FR-012 / US2 AC2: after wire_frame_too_large the pump must call "
        << "session.close(terminal) → FSM must reach Disconnected. "
        << "state=" << static_cast<int>(st) << " next_inbound=" << next_inbound
        << ". Removing the pump's over-capacity arm leaves state != Disconnected.";

    // Additional no-silent-truncation guard: the oversized frame must NOT
    // have partially advanced next_inbound (FR-012).
    EXPECT_LE(next_inbound, 2)
        << "FR-012 / US2 AC2 (no-silent-truncation): an oversized frame must NOT "
        << "advance next_inbound_unsafe past the post-Logon value of 2. "
        << "next_inbound=" << next_inbound;
}

// ─────────────────────────────────────────────────────────────────────────────
// Case 3 — EofDisconnectsSession
//
// After logon, client closes the TCP connection cleanly (EOF).
// The real pump detects transport_read_eof from async_read_some, calls
// session.close(terminal), which transitions the FSM to Disconnected.
//
// GREEN (T015): pump detects EOF → close(terminal) → state == Disconnected,
//   OR the session is already freed (nullptr from lookup() — also terminal).
//
// This assertion FAILS if the pump's EOF arm is deleted:
//   without it, async_read_some would eventually return EOF, the pump would
//   not act on it, the session stays in Active/LogonReceived for the full 4s
//   window (heartbeat_interval = 30s), and state != Disconnected.
// ─────────────────────────────────────────────────────────────────────────────
TEST(EngineReadPumpTest, EofDisconnectsSession) {
    asio::io_context ioc;
    auto h = build_harness(ioc);
    if (!h) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(h->engine->start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t port = h->engine->acceptor_bound_endpoint(h->acc_id).port;
    ASSERT_NE(port, 0U) << "acceptor listener did not bind";

    // Client sends only the Logon and then immediately closes (EOF).
    asio::co_spawn(ioc,
                   run_client_with_extra_frames(ioc, *h->fixture, port, "INITIATOR", "ACCEPTOR",
                                                /*extra_frames=*/{},
                                                /*wait_after=*/200ms),  // close quickly after Logon
                   asio::detached);

    // Allow up to 4s for the session to detect the EOF and disconnect.
    ioc.run_for(4s);
    ioc.restart();

    auto acc = h->engine->lookup(h->acc_id);
    if (!acc) {
        // Session already freed — reached terminal state and was cleaned up.
        // This is the GREEN outcome: the pump drove the session through close().
        auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
        if (!fixpp::test_support::run_to_exhaustion_or_report(
                ioc, stop_fut, "EngineReadPumpTest::EofDisconnectsSession/stop_fut1")) {
            return;
        }
        stop_fut.get();
        SUCCEED() << "Session already freed (terminal state reached) — GREEN path.";
        return;
    }

    auto st = acc->state();
    auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "EngineReadPumpTest::EofDisconnectsSession/stop_fut2")) {
        return;
    }
    stop_fut.get();

    // STRENGTHENED GREEN assertion (T015): pump must have detected the client EOF
    // (transport_read_eof from async_read_some) and called close(terminal),
    // driving the FSM to Disconnected within the 4s window.
    // Removing the pump's EOF arm leaves state == Active or LogonReceived
    // (heartbeat_interval=30s >> 4s window) — this EXPECT_EQ would then FAIL.
    EXPECT_EQ(st, fsm_state::Disconnected)
        << "US2 AC3 / FR-012: after client EOF the pump must call close(terminal) "
        << "→ FSM must reach Disconnected within the 4s window. "
        << "state=" << static_cast<int>(st) << ". "
        << "Removing the pump's EOF arm leaves state != Disconnected "
        << "(heartbeat_interval=30s >> 4s → no other path drives Disconnected).";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cases 4-5 — #348: the PEER of a fixpp session closing down must read a clean
// TLS EOF, not an OS-level error.
//
// WHY THESE LIVE IN THE READ-PUMP FILE. The defect is a property of the pump's
// state at teardown, not of the transport in isolation. Both teardown sites --
// Engine::stop()'s per-session transport close and Session::close(terminal)'s
// post-root-cancel close -- fire while the pump is SUSPENDED in
// async_read_some, and the synchronous close() refuses to send the close-notify
// alert whenever an SSL op is suspended (mutating SSL state under a pending
// completion is the BIO_ctrl hazard close() documents). So the alert was skipped
// at exactly the two sites that produce every normal disconnect, and a peer of a
// cleanly closing fixpp session read transport_read_error (104) -- measured,
// deterministic, and normalised for long enough that alerting tuned to the
// documented non-fatal truncation mis-classified every clean shutdown.
//
// The transport-level cells in tests/transport/test_inflight_exclusivity.cpp
// witness that close_async() quiesces a suspended read and still drains the
// alert. These two witness that the TEARDOWN PATHS CALL IT. Reverting either
// adoption to close() leaves every transport-level cell green.
//
// ⚠️ The assertion is on the PEER's read outcome, deliberately: what reaches the
// wire is the only thing that can tell a drained BIO from a discarded one from
// outside the process, and "the close returned {}" is true of the defect too.
//
// ⚠️ RED-ARM CONTRACT:
//   case 4 — revert engine.cpp's stop() step-2 lambda to `tp->close()`.
//   case 5 — revert session.cpp's post-root-cancel close to `live->close()`.
// Each must then report transport_read_error, and ONLY its own case must fail.
// ─────────────────────────────────────────────────────────────────────────────

// A client that logs on and then keeps reading until the connection ends. The
// terminal read outcome is what the two cases assert on. Reading in a loop (not
// once) is load-bearing: the acceptor answers the Logon, and a single read would
// resolve on that reply rather than on the shutdown.
struct HoldingClient {
    std::unique_ptr<fixpp::transport::Transport> transport;
    std::optional<fixpp::core::expected_t<std::size_t>> terminal_read;
    bool logged_on = false;
};

static asio::awaitable<void> run_client_holding_read(
    fixpp::transport::test::LoopbackTlsFixture& fixture, uint16_t acceptor_port,
    HoldingClient& hc) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(hc.transport.get());
        if (!tls) co_return;

        fixpp::transport::Endpoint ep{"127.0.0.1", acceptor_port};
        auto conn_r = co_await hc.transport->async_connect(ep);
        if (!conn_r.has_value()) co_return;
        auto hs_r = co_await tls->async_handshake(fixture.ssl_cfg());
        if (!hs_r.has_value()) co_return;

        auto logon = make_logon_frame("FIX.4.2", "INITIATOR", "ACCEPTOR");
        auto w_r = co_await hc.transport->async_write(std::span<const std::byte>{logon});
        if (!w_r.has_value()) co_return;
        hc.logged_on = true;

        std::array<std::byte, 512> buf{};
        for (;;) {
            auto r = co_await hc.transport->async_read_some(std::span<std::byte>{buf});
            if (!r.has_value()) {
                hc.terminal_read = r;
                co_return;
            }
        }
    } catch (...) {
    }
}

// Shared assertion so the two cases cannot drift apart in what they demand.
static void expect_clean_peer_eof(HoldingClient const& hc, const char* site) {
    ASSERT_TRUE(hc.logged_on) << "client never completed connect+handshake+Logon — the case below "
                                 "would be asserting on a connection that never existed";
    ASSERT_TRUE(hc.terminal_read.has_value())
        << "the peer's read never terminated; nothing closed the connection";
    ASSERT_FALSE(hc.terminal_read->has_value());
    EXPECT_EQ(hc.terminal_read->error(), fixpp::core::error::transport_read_eof)
        << "#348: " << site
        << " must deliver the TLS close-notify. transport_read_error (104) here means the "
           "teardown used the synchronous close(), which skips the alert whenever an SSL op is "
           "suspended — and the read pump is always suspended at this point.";
}

TEST(EngineReadPumpTest, EngineStopDeliversCloseNotifyToPeer_Fixes348) {
    asio::io_context ioc;
    auto h = build_harness(ioc);
    if (!h) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(h->engine->start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t port = h->engine->acceptor_bound_endpoint(h->acc_id).port;
    ASSERT_NE(port, 0U) << "acceptor listener did not bind";

    HoldingClient hc;
    hc.transport = h->fixture->make_client(ioc.get_executor());
    asio::co_spawn(ioc, run_client_holding_read(*h->fixture, port, hc), asio::detached);

    ioc.run_for(2s);
    ioc.restart();

    ASSERT_TRUE(hc.logged_on) << "client did not reach Logon within 2s";
    ASSERT_FALSE(hc.terminal_read.has_value())
        << "the peer's read already terminated BEFORE stop() — this case would then witness "
           "whatever ended it, not the teardown";

    auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "EngineReadPumpTest::EngineStopDeliversCloseNotifyToPeer_Fixes348")) {
        return;
    }
    stop_fut.get();

    expect_clean_peer_eof(hc, "Engine::stop()'s per-session transport close");
}

TEST(EngineReadPumpTest, SessionTerminalCloseDeliversCloseNotifyToPeer_Fixes348) {
    asio::io_context ioc;
    auto h = build_harness(ioc);
    if (!h) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(h->engine->start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    uint16_t port = h->engine->acceptor_bound_endpoint(h->acc_id).port;
    ASSERT_NE(port, 0U) << "acceptor listener did not bind";

    HoldingClient hc;
    hc.transport = h->fixture->make_client(ioc.get_executor());
    asio::co_spawn(ioc, run_client_holding_read(*h->fixture, port, hc), asio::detached);

    ioc.run_for(2s);
    ioc.restart();

    // ⚠️ ASSERT, not GTEST_SKIP. The sibling cells in this file skip here because
    // they predate the acceptance path being wired; it is live now, and this cell
    // is the ONLY witness that session.cpp's teardown calls close_async(). A skip
    // would let a regression that stops publishing the session delete the witness
    // silently and still report green. The fixture-dir skip above is the only
    // legitimate skip in this cell.
    auto acc = h->engine->lookup(h->acc_id);
    ASSERT_TRUE(acc) << "acceptor session was never published — this cell cannot witness "
                        "Session::close(terminal) without it, and skipping here would hide the "
                        "loss of the only witness for that adoption";
    ASSERT_TRUE(hc.logged_on) << "client did not reach Logon within 2s";
    ASSERT_FALSE(hc.terminal_read.has_value())
        << "the peer's read already terminated BEFORE close(terminal)";

    // THE DIFFERENCE FROM CASE 4: the session closes itself, so the close runs
    // at session.cpp's post-root-cancel site rather than Engine::stop()'s.
    auto close_fut =
        asio::co_spawn(ioc, acc->close(fixpp::session::close_mode::terminal), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(
            ioc, close_fut, 4s, "SessionTerminalCloseDeliversCloseNotifyToPeer/close")) {
        fixpp::test_support::cancel_and_drain_or_report(
            ioc, *h->engine->clock(), "SessionTerminalCloseDeliversCloseNotifyToPeer/close");
        // The 4 s window is the one the ASSERT this replaces named; the report text is
        // deliberately just the stem plus the label, so the drivers match one shape.
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "SessionTerminalCloseDeliversCloseNotifyToPeer/close";
        return;
    }
    (void)close_fut.get();

    expect_clean_peer_eof(hc, "Session::close(terminal)'s post-root-cancel transport close");

    auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut,
            "EngineReadPumpTest::SessionTerminalCloseDeliversCloseNotifyToPeer_Fixes348")) {
        return;
    }
    stop_fut.get();
}

// ─────────────────────────────────────────────────────────────────────────────
// Cases 6-8 — 093-inbound-frame-dispositions (FR-001; contract C-1). A §4.5.2
// framing failure the Framer detects no longer ends an ESTABLISHED session: the
// pump's Framer resyncs, the garbled frame is disregarded, and the session carries
// on. These cells superseded 092's FramerFailureClosesEstablishedSession_* pins of
// the close (L-004-4 closes).
//
// Each cell feeds one frame the Framer finds garbled, after the peer has read the
// acceptor's Logon reply and the acceptor reports Active, and then a conformant
// Heartbeat at the number the garbled frame carried:
//   - a bad CheckSum (wire_checksum_mismatch);
//   - a BodyLength too small, so `10=` is not where BodyLength puts it
//     (wire_invalid_body_length);
//   - a BeginString prefix whose first byte is not `8` (wire_framing_resync).
// A well-formed wrong BeginString is not a cell: it passes the Framer and Guard 2
// closes the session whatever the pump does. A too-large BodyLength is not a cell
// either: the Framer waits for more bytes (EngineReadPumpResync's L-1 pin).
//
// Each cell first feeds its bytes to a standalone resync-mode Framer and checks the
// region's kind, so a cell cannot pass on a different Framer arm than the one it names.
//
// Observations, all taken before Engine::stop():
//   - the session state (Active);
//   - whether the peer's read ended, i.e. the acceptor closed the connection (it
//     must not have);
//   - NextNumIn: the garbled frame does not advance it, and the conformant Heartbeat
//     at the same number does;
//   - Session::garbled_frame_count(), which counts the one region.
//
// Mutant (run in a scratch copy): in src/session/engine.cpp's run_read_pump, pass a
// Framer::Config with resync_on_garble = false. Each of these cells must then fail
// with a clean test failure (exit 1, not an abort).
// ─────────────────────────────────────────────────────────────────────────────

namespace {

constexpr auto kGarbleBudget = 4s;
// Bound on the client's wait for the acceptor to report Active. It shares
// kGarbleBudget with connect, handshake and the rest of the cell, so it is a fraction
// of it; heartbeat_interval (build_harness) is far above both.
constexpr auto kActiveWaitBudget = kGarbleBudget / 4;
static_assert(kActiveWaitBudget * 2 < kGarbleBudget,
              "the Active wait must leave most of kGarbleBudget to the rest of the cell");
constexpr auto kActivePollStep = 1ms;

struct GarbledFrameClient {
    std::unique_ptr<fixpp::transport::Transport> transport;
    std::shared_ptr<fixpp::session::Session> acc;  // leased once the Logon reply is read
    std::optional<fsm_state> state_before_garble;
    bool saw_logon_reply = false;
    bool sent_garble_and_follow_up = false;
    std::optional<fixpp::core::expected_t<std::size_t>> terminal_read;
};

// Logs on, reads until the acceptor's Logon reply, waits for the acceptor to leave
// LogonReceived, sends `garbled` then `follow_up` in one write, then reads until the
// connection ends. It never closes its own side.
asio::awaitable<void> run_client_garbled_frame(fixpp::transport::test::LoopbackTlsFixture& fixture,
                                               uint16_t acceptor_port,
                                               fixpp::session::Engine& engine,
                                               fixpp::session::SessionId acc_id,
                                               std::vector<std::byte> garbled,
                                               GarbledFrameClient& fc) {
    co_await asio::this_coro::reset_cancellation_state(asio::enable_total_cancellation());
    try {
        auto* tls = dynamic_cast<fixpp::transport::TlsTransport*>(fc.transport.get());
        if (!tls) co_return;

        fixpp::transport::Endpoint ep{"127.0.0.1", acceptor_port};
        auto conn_r = co_await fc.transport->async_connect(ep);
        if (!conn_r.has_value()) co_return;
        auto hs_r = co_await tls->async_handshake(fixture.ssl_cfg());
        if (!hs_r.has_value()) co_return;

        auto logon = make_logon_frame("FIX.4.2", "INITIATOR", "ACCEPTOR");
        auto w_r = co_await fc.transport->async_write(std::span<const std::byte>{logon});
        if (!w_r.has_value()) co_return;

        std::array<std::byte, 512> buf{};
        std::string received;
        constexpr std::string_view kLogonReply =
            "\x01"
            "35=A\x01";
        while (!received.contains(kLogonReply)) {
            auto r = co_await fc.transport->async_read_some(std::span<std::byte>{buf});
            if (!r.has_value()) {
                fc.terminal_read = r;
                co_return;
            }
            received.append(reinterpret_cast<const char*>(buf.data()), *r);
        }
        fc.saw_logon_reply = true;
        fc.acc = engine.lookup(acc_id);
        // The peer's read of the reply and the acceptor's resumption after its reply
        // write are separate completions on this io_context, and either may run first;
        // the acceptor enters Active only on the latter. Yield until it leaves
        // LogonReceived, bounded, so the garbled frame meets an established session. A
        // session still in LogonReceived at the bound is recorded as such and fails the
        // precondition check.
        if (fc.acc) {
            asio::steady_timer poll{co_await asio::this_coro::executor};
            auto const active_by = std::chrono::steady_clock::now() + kActiveWaitBudget;
            while (fc.acc->state() == fsm_state::LogonReceived &&
                   std::chrono::steady_clock::now() < active_by) {
                poll.expires_after(kActivePollStep);
                co_await poll.async_wait(asio::use_awaitable);
            }
            fc.state_before_garble = fc.acc->state();
        }

        // The conformant Heartbeat at the number the garbled frame carried.
        auto const follow_up = make_heartbeat_frame("FIX.4.2", 2, "INITIATOR", "ACCEPTOR");
        garbled.insert(garbled.end(), follow_up.begin(), follow_up.end());
        auto f_r = co_await fc.transport->async_write(std::span<const std::byte>{garbled});
        if (!f_r.has_value()) co_return;
        fc.sent_garble_and_follow_up = true;

        for (;;) {
            auto r = co_await fc.transport->async_read_some(std::span<std::byte>{buf});
            if (!r.has_value()) {
                fc.terminal_read = r;
                co_return;
            }
        }
    } catch (...) {
    }
}

// What a resync-mode Framer makes of `bytes` followed by a conformant Heartbeat, fed
// whole: the regions its first feed opened and the first one's kind.
fixpp::wire::garble_summary resync_garbles_for(std::vector<std::byte> bytes) {
    auto const hb = make_heartbeat_frame("FIX.4.2", 2, "INITIATOR", "ACCEPTOR");
    bytes.insert(bytes.end(), hb.begin(), hb.end());
    fixpp::wire::pmr_carry_buffer carry{bytes.size() + 1, std::pmr::new_delete_resource()};
    std::array<fixpp::wire::frame_view, 1> out{};
    fixpp::wire::Framer::Config c;
    c.resync_on_garble = true;
    fixpp::wire::Framer framer{c};
    (void)framer.feed(std::span<const std::byte>{bytes}, carry,
                      std::span<fixpp::wire::frame_view>{out});
    return framer.last_garbles();
}

// A conformant Heartbeat at MsgSeqNum 2 as text, for the cells to corrupt.
std::string heartbeat_text() {
    auto const hb = make_heartbeat_frame("FIX.4.2", 2, "INITIATOR", "ACCEPTOR");
    return std::string{reinterpret_cast<const char*>(hb.data()), hb.size()};
}

std::vector<std::byte> to_bytes(std::string const& s) {
    std::vector<std::byte> out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<std::byte>(c));
    return out;
}

void run_framer_garble_cell(std::vector<std::byte> const& garbled, fixpp::core::error expected,
                            const char* label) {
    // The bytes must trip the Framer arm the cell names, as one region.
    auto const probe = resync_garbles_for(garbled);
    EXPECT_EQ(probe.regions, 1U) << label << ": the garbled frame must be one Framer region";
    EXPECT_EQ(probe.first_kind, expected)
        << label << ": the frame does not trip the Framer arm this cell names";

    asio::io_context ioc;
    auto h = build_harness(ioc);
    if (!h) {
        GTEST_SKIP() << "FIXPP_TLS_FIXTURE_DIR not set";
    }

    ASSERT_TRUE(h->engine->start().has_value()) << "engine.start() failed";
    ioc.run_for(50ms);
    ioc.restart();

    auto const next_inbound_of = [](fixpp::session::Session& s) {
        return static_cast<int>(
            fixpp::session::session_test_access::seqnum_mgr(s).next_inbound_unsafe());
    };

    uint16_t const port = h->engine->acceptor_bound_endpoint(h->acc_id).port;
    GarbledFrameClient fc;
    fc.transport = h->fixture->make_client(ioc.get_executor());
    if (port != 0U) {
        asio::co_spawn(
            ioc, run_client_garbled_frame(*h->fixture, port, *h->engine, h->acc_id, garbled, fc),
            asio::detached);
        auto const deadline = std::chrono::steady_clock::now() + kGarbleBudget;
        while (!fc.terminal_read.has_value() && std::chrono::steady_clock::now() < deadline &&
               !(fc.sent_garble_and_follow_up && fc.acc && next_inbound_of(*fc.acc) == 3)) {
            ioc.run_for(20ms);
            ioc.restart();
        }
        // Whatever else the garble could cause is queued by now; let it run.
        ioc.run_for(50ms);
        ioc.restart();
    }

    // Snapshots, taken before stop() and before any fatal assertion.
    bool const peer_read_ended = fc.terminal_read.has_value();
    std::optional<fsm_state> const state_after =
        fc.acc ? std::optional<fsm_state>{fc.acc->state()} : std::nullopt;
    std::optional<int> const next_inbound =
        fc.acc ? std::optional<int>{next_inbound_of(*fc.acc)} : std::nullopt;
    std::optional<std::uint64_t> const garbles =
        fc.acc ? std::optional<std::uint64_t>{fc.acc->garbled_frame_count()} : std::nullopt;
    fc.acc.reset();  // release the lease before the engine is destroyed

    auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(ioc, stop_fut, label)) {
        return;
    }
    stop_fut.get();

    ASSERT_NE(port, 0U) << label << ": acceptor listener did not bind";
    ASSERT_TRUE(fc.saw_logon_reply) << label << ": the client never read the Logon reply";
    ASSERT_TRUE(fc.sent_garble_and_follow_up) << label << ": the garbled frame was never written";
    EXPECT_EQ(fc.state_before_garble, std::optional<fsm_state>{fsm_state::Active})
        << label << ": the session must be established when the garbled frame is sent";
    EXPECT_EQ(state_after, std::optional<fsm_state>{fsm_state::Active})
        << label << ": FR-001: a Framer garble must not end an established session";
    EXPECT_FALSE(peer_read_ended) << label << ": the acceptor must keep the connection open";
    EXPECT_EQ(next_inbound, std::optional<int>{3})
        << label
        << ": the garbled frame is disregarded and the conformant Heartbeat at the "
           "same number is processed";
    EXPECT_EQ(garbles, std::optional<std::uint64_t>{1U}) << label << ": one garbled region";
}

}  // namespace

TEST(EngineReadPumpTest, FramerGarbleDisregardedInEstablishedSession_BadCheckSum) {
    std::string s = heartbeat_text();
    // The last field is `10=NNN<SOH>`; NNN + 1 (mod 256) is a CheckSum that does not match.
    auto const digits_at = s.size() - 4;
    auto const cs = static_cast<unsigned>(std::stoi(s.substr(digits_at, 3)));
    std::array<char, 4> wrong{};
    std::snprintf(wrong.data(), wrong.size(), "%03u", (cs + 1U) % 256U);
    s.replace(digits_at, 3, wrong.data(), 3);
    run_framer_garble_cell(to_bytes(s), fixpp::core::error::wire_checksum_mismatch,
                           "FramerGarbleDisregardedInEstablishedSession_BadCheckSum");
}

TEST(EngineReadPumpTest, FramerGarbleDisregardedInEstablishedSession_BodyLengthTooSmall) {
    std::string s = heartbeat_text();
    // Rewrite 9=<n> as 9=<n - 5>: the Framer then looks for `10=` five bytes early.
    auto const len_at = s.find(
                            "\x01"
                            "9=") +
                        3;
    auto const len_end = s.find('\x01', len_at);
    int const body_len = std::stoi(s.substr(len_at, len_end - len_at));
    s.replace(len_at, len_end - len_at, std::to_string(body_len - 5));
    run_framer_garble_cell(to_bytes(s), fixpp::core::error::wire_invalid_body_length,
                           "FramerGarbleDisregardedInEstablishedSession_BodyLengthTooSmall");
}

TEST(EngineReadPumpTest, FramerGarbleDisregardedInEstablishedSession_MalformedBeginStringPrefix) {
    std::string s = heartbeat_text();
    s[0] = 'X';  // `X=FIX.4.2`: the first byte is not `8`
    run_framer_garble_cell(
        to_bytes(s), fixpp::core::error::wire_framing_resync,
        "FramerGarbleDisregardedInEstablishedSession_MalformedBeginStringPrefix");
}

// ─────────────────────────────────────────────────────────────────────────────
// 093-inbound-frame-dispositions T027 — Q-2, Q-3 and the L-1 pin, through the pump
// (contract C-1; FR-001, FR-002). The pump's Framer resyncs: a garbled region is
// disregarded and framing resumes at the next "8=FIX", so the session carries on. Base
// RED: the pump closes the session at the first garbled byte.
//
// These cells drive a plaintext acceptor on a mock engine clock (plain_engine_rig.hpp),
// so none can skip for a missing TLS fixture. Every observation is taken before
// Engine::stop() and none is fatal until stop() has returned: a fatal assertion between
// start() and a completed stop() aborts in ~Engine instead of failing the cell.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

namespace pr = fixpp::test_support::plain_rig;

// A plaintext acceptor, logged on and Active.
struct ResyncCell {
    pr::Rig rig;
    bool up = false;

    explicit ResyncCell(std::optional<std::uint32_t> advertised = std::nullopt) {
        auto cfg = rig.cfg();
        cfg.advertised_max_message_size = advertised;
        up = rig.start(std::move(cfg)) && rig.to_active();
    }

    // The session's inbound limit L and its carry's capacity (093, data-model E-2); 0
    // without a session.
    [[nodiscard]] std::size_t limit() const {
        auto const s = rig.session();
        return s ? fixpp::session::session_test_access::inbound_limit(*s) : 0U;
    }
    [[nodiscard]] std::size_t carry_capacity() const {
        auto const s = rig.session();
        return s ? fixpp::session::session_test_access::carry_capacity(*s) : 0U;
    }

    [[nodiscard]] std::uint32_t next_in() const {
        auto const s = rig.session();
        return s ? static_cast<std::uint32_t>(
                       fixpp::session::session_test_access::seqnum_mgr(*s).next_inbound_unsafe())
                 : 0U;
    }
    [[nodiscard]] std::uint64_t garbles() const {
        auto const s = rig.session();
        return s ? s->garbled_frame_count() : 0U;
    }
    [[nodiscard]] bool wait_next_in(std::uint32_t n) {
        return rig.run_until([&] { return next_in() == n; });
    }
    // Writes `part`, waits for the write, then gives the pump a short settle to read it.
    // The settle is not a barrier: the next write may still coalesce into the same read.
    [[nodiscard]] bool write_part(std::string part) {
        rig.peer.send(std::move(part));
        if (!rig.run_until([&] { return rig.peer.all_written(); })) return false;
        rig.settle(std::chrono::milliseconds{2});
        return true;
    }
};

}  // namespace

// Q-2: garbage that does not end in SOH, glued to the next frame's "8=FIX". Under an
// SOH-anchored start rule the next frame would be lost with the garbage.
TEST(EngineReadPumpResync, Q2_GarbageNotEndingInSohBetweenGoodFramesInOneWrite) {
    ResyncCell c;
    bool const d = c.up && c.rig.deliver(c.rig.heartbeat(2) + "XYZ" + c.rig.heartbeat(3));
    bool const processed = d && c.wait_next_in(4);
    auto const st = c.rig.state();
    auto const n = c.garbles();
    bool const read_ended = c.rig.peer.read_ended;
    c.rig.stop();

    ASSERT_TRUE(c.up && d) << "setup";
    EXPECT_TRUE(processed) << "both good frames are processed";
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_FALSE(read_ended) << "the connection stays open";
    EXPECT_EQ(n, 1U) << "the garbage is one garbled region";
}

// Q-2: a truncated frame, then the same frame whole. The truncated candidate's
// BodyLength runs into the whole frame, so `10=` is not at its counted offset; the
// search then finds the whole frame from the candidate's second byte.
TEST(EngineReadPumpResync, Q2_TruncatedFrameThenAGoodFrame) {
    ResyncCell c;
    std::string const hb3 = c.up ? c.rig.heartbeat(3) : std::string{};
    bool const d = c.up && c.rig.deliver(c.rig.heartbeat(2) + hb3.substr(0, 30) + hb3);
    bool const processed = d && c.wait_next_in(4);
    auto const st = c.rig.state();
    auto const n = c.garbles();
    c.rig.stop();

    ASSERT_TRUE(c.up && d) << "setup";
    EXPECT_TRUE(processed) << "the whole frame after the truncated one is processed";
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_EQ(n, 1U) << "the truncated frame is one garbled region";
}

// Q-2, segmentation independence: each shape above, split into two writes at every byte
// boundary, on one session. Each split is one more garbled region and one more good
// frame, whatever the split point, and whether or not the two writes coalesce into one
// read, so the cell holds either way. The deterministic per-boundary splits of the
// Framer itself are its own cells (tests/wire/framer_resync_test.cpp).
void run_every_split(std::function<std::string(pr::Rig const&, std::uint32_t)> const& shape,
                     const char* name) {
    ResyncCell c;
    std::uint32_t seq = 2;
    std::uint64_t splits = 0;
    std::optional<std::size_t> first_bad_split;
    bool ok = c.up;
    for (std::size_t k = 1; ok; ++k) {
        std::string const b = shape(c.rig, seq);
        if (k >= b.size()) break;
        ok = c.write_part(b.substr(0, k)) && c.write_part(b.substr(k)) && c.wait_next_in(seq + 1U);
        ++splits;
        if (ok && c.garbles() != splits && !first_bad_split) first_bad_split = k;
        ++seq;
    }
    auto const st = c.rig.state();
    auto const n = c.garbles();
    c.rig.stop();

    ASSERT_TRUE(c.up) << name << ": setup";
    EXPECT_TRUE(ok) << name << ": a split left its good frame unprocessed (split " << splits << ")";
    EXPECT_EQ(first_bad_split, std::nullopt)
        << name << ": the garble count went wrong at this split";
    EXPECT_EQ(n, splits) << name;
    EXPECT_EQ(st, fsm_state::Active) << name;
}

TEST(EngineReadPumpResync, Q2_GarbageNotEndingInSoh_EverySplitPoint) {
    run_every_split(
        [](pr::Rig const& rig, std::uint32_t seq) { return "XYZ" + rig.heartbeat(seq); },
        "garbage not ending in SOH");
}

TEST(EngineReadPumpResync, Q2_TruncatedFrame_EverySplitPoint) {
    run_every_split(
        [](pr::Rig const& rig, std::uint32_t seq) {
            std::string const hb = rig.heartbeat(seq);
            return hb.substr(0, 30) + hb;
        },
        "truncated frame");
}

// Q-2: a garbage-only stream, twice the pump's carry, holds no "8=FIX", so the search
// keeps nothing of it: it is consumed in finite steps without overflowing the carry,
// with the session up, and the frame after it is processed.
TEST(EngineReadPumpResync, Q2_GarbageOnlyStreamIsConsumedWithTheSessionUp) {
    ResyncCell c;
    std::size_t const kGarbage = 2U * c.carry_capacity();
    constexpr std::size_t kChunk = 4096;
    bool ok = c.up && kGarbage != 0U;
    for (std::size_t sent = 0; ok && sent < kGarbage; sent += kChunk) {
        ok = c.write_part(std::string(kChunk, 'Q'));
    }
    bool const d = ok && c.rig.deliver(c.rig.heartbeat(2));
    bool const processed = d && c.wait_next_in(3);
    auto const st = c.rig.state();
    auto const n = c.garbles();
    bool const read_ended = c.rig.peer.read_ended;
    c.rig.stop();

    ASSERT_TRUE(c.up && ok && d) << "setup";
    EXPECT_TRUE(processed) << "the frame after the garbage is processed";
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_FALSE(read_ended) << "the garbage does not close the connection";
    EXPECT_EQ(n, 1U) << "one region, continued across every read";
}

// Q-3 (L-15): a structurally complete frame whose CheckSum is wrong is discarded through
// its own end, with the well-formed Heartbeat(34=3) its body carries. Were the embedded
// frame delivered, NextNumIn would reach 4 from it and the real Heartbeat(34=3) after
// the outer frame would be too low and end the session.
TEST(EngineReadPumpResync, Q3_WrongCheckSumFrameIsDiscardedWholeWithTheFrameInItsExtent) {
    ResyncCell c;
    std::string outer;
    if (c.up) {
        outer = c.rig.msg("0", 3, "58=" + c.rig.heartbeat(3));
        auto const at = outer.rfind("10=") + 3;
        outer.replace(at, 3, outer.substr(at, 3) == "000" ? "001" : "000");
    }
    bool const d = c.up && c.rig.deliver(c.rig.heartbeat(2) + outer + c.rig.heartbeat(3));
    bool const processed = d && c.wait_next_in(4);
    c.rig.settle();
    auto const st = c.rig.state();
    auto const n = c.garbles();
    auto const next = c.next_in();
    c.rig.stop();

    ASSERT_TRUE(c.up && d) << "setup";
    EXPECT_TRUE(processed) << "the Heartbeat after the outer frame is processed";
    EXPECT_EQ(next, 4U) << "only Heartbeat(2) and the real Heartbeat(3) advanced NextNumIn";
    EXPECT_EQ(st, fsm_state::Active) << "the embedded frame was not delivered";
    EXPECT_EQ(n, 1U) << "the outer frame, with what it carries, is one garbled region";
}

// Q-3 (L-8): a well-formed frame lying after a malformed candidate's first byte is
// found by the search and delivered, and the session's guards then process it as any
// frame: here Heartbeat(34=3), in sequence.
TEST(EngineReadPumpResync, Q3_AFrameEmbeddedAfterAMalformedCandidateIsDeliveredAndGuarded) {
    ResyncCell c;
    std::string const candidate =
        "8=FIX.4.2\x01"
        "9=5\x01"
        "X";
    bool const d = c.up && c.rig.deliver(c.rig.heartbeat(2) + candidate + c.rig.heartbeat(3));
    bool const processed = d && c.wait_next_in(4);
    auto const st = c.rig.state();
    auto const n = c.garbles();
    c.rig.stop();

    ASSERT_TRUE(c.up && d) << "setup";
    EXPECT_TRUE(processed) << "the embedded Heartbeat(3) is delivered and processed";
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_EQ(n, 1U) << "the malformed candidate is one garbled region";
}

// The L-1 pin. A BodyLength too large for its frame, but within the session's limit L
// and its carry, stalls the pump until the counted bytes arrive: the Heartbeat(34=3)
// the count swallows is not processed meanwhile. When they arrive, `10=` is not at the
// counted offset, so the candidate is disregarded and framing resumes inside it, at
// Heartbeat(34=3). The filler after Heartbeat(3) then opens a second region at that
// frame boundary, and Heartbeat(4) follows it. The sizes are read from L and the carry
// open() allocated, through the engine seam the pump reads them from, never from a
// literal. The carry holds L plus one read (data-model E-2), spelled out here.
TEST(EngineReadPumpResync, L1_ATooLargeBodyLengthWithinTheLimitStallsThenIsDisregarded) {
    ResyncCell c;
    std::size_t const limit = c.limit();
    std::size_t const carry = c.carry_capacity();
    std::size_t const kBodyLength = limit / 2U;
    std::string const candidate =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(kBodyLength) +
        "\x01"
        "35=0\x01";
    bool const d1 = c.up && kBodyLength != 0U &&
                    c.rig.deliver(c.rig.heartbeat(2) + candidate + c.rig.heartbeat(3));
    bool const hb2 = d1 && c.wait_next_in(3);
    c.rig.settle();
    auto const stalled_next = c.next_in();
    auto const stalled_garbles = c.garbles();
    bool const d2 = hb2 && c.rig.deliver(std::string(kBodyLength, 'Z') + c.rig.heartbeat(4));
    bool const resumed = d2 && c.wait_next_in(5);
    auto const st = c.rig.state();
    auto const n = c.garbles();
    c.rig.stop();

    ASSERT_TRUE(c.up && d1 && hb2 && d2) << "setup";
    EXPECT_EQ(carry, limit + 4096U) << "the carry holds L plus one read";
    EXPECT_EQ(stalled_next, 3U) << "the stall holds Heartbeat(3) until the counted bytes arrive";
    EXPECT_EQ(stalled_garbles, 0U) << "nothing is decided while the candidate is partial";
    EXPECT_TRUE(resumed) << "framing resumes once the counted bytes arrive";
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_EQ(n, 2U) << "the candidate, and the filler at the boundary after Heartbeat(3)";
}

// The L-1 pin's other half, re-based by 093 (Q-6): the carry holds L plus one read,
// and a candidate whose BodyLength makes its frame longer than L is refused as soon as
// the BodyLength is read, so a carry overflow cannot happen in the pump. A BodyLength
// of L closes the session and the connection, without waiting for the body.
TEST(EngineReadPumpResync, L1_AnOverLBodyLengthClosesAtItsHeader) {
    ResyncCell c;
    std::size_t const limit = c.limit();
    std::string const candidate =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(limit) +
        "\x01"
        "35=0\x01";
    bool const d1 = c.up && limit != 0U && c.rig.deliver(c.rig.heartbeat(2) + candidate);
    bool const hb2 = d1 && c.wait_next_in(3);
    bool const closed = hb2 && c.rig.run_until([&] {
        return c.rig.state() == fsm_state::Disconnected && c.rig.peer.read_ended;
    });
    c.rig.stop();

    ASSERT_TRUE(c.up && d1 && hb2) << "setup";
    EXPECT_TRUE(closed) << "an over-L BodyLength closes the session and the connection";
}

// ── Q-12 (T055a): a frame of exactly L, split near the carry's edge ─────────
//
// A Heartbeat padded with one Text(58) field to exactly L = 65536 bytes (few fields,
// so the base's parse holds it), then Heartbeat(next). Each split writes the first k
// bytes, waits until the pump has read them, then writes the rest with the next
// Heartbeat, which loopback delivers as one read when it fits one. The pump then holds
// k bytes and must take 65536 - k + the Heartbeat in one feed: a carry of exactly L
// overflows there, a carry of L plus one read does not. The splits run from the first
// at which the rest and the Heartbeat fit one read to the frame's last byte, in steps,
// and every split of the last 64 bytes. Base RED: the base's 64 KiB carry overflows
// and the session closes.
TEST(EngineReadPumpResync, Q12_AFrameOfExactlyLSplitNearTheCarryEdgeIsDelivered) {
    ResyncCell c;
    constexpr std::size_t kLimit = 65536;
    constexpr std::size_t kRead = 4096;
    std::size_t const hb_size = c.up ? c.rig.heartbeat(100).size() : 0U;
    std::vector<std::size_t> splits;
    for (std::size_t k = kLimit + hb_size - kRead; k < kLimit - 64U; k += 509U) splits.push_back(k);
    for (std::size_t k = kLimit - 64U; k < kLimit; ++k) splits.push_back(k);

    std::uint32_t seq = 2;
    std::optional<std::size_t> first_lost;
    bool ok = c.up;
    for (std::size_t const k : splits) {
        if (!ok) break;
        std::string const f = c.rig.msg_of_size("0", seq, {}, kLimit, "58", false);
        std::string const next = c.rig.heartbeat(seq + 1U);
        ok = !f.empty() && c.write_part(f.substr(0, k)) &&
             c.rig.run_until([&] { return c.rig.peer.all_written(); });
        c.rig.settle(std::chrono::milliseconds{5});
        ok = ok && c.write_part(f.substr(k) + next);
        bool const delivered = ok && c.wait_next_in(seq + 2U);
        if (!delivered && !first_lost) first_lost = k;
        ok = ok && delivered;
        seq += 2U;
    }
    auto const st = c.rig.state();
    bool const read_ended = c.rig.peer.read_ended;
    c.rig.stop();

    ASSERT_TRUE(c.up) << "setup";
    EXPECT_EQ(first_lost, std::nullopt) << "a split lost the frame of exactly L";
    EXPECT_TRUE(ok);
    EXPECT_EQ(st, fsm_state::Active);
    EXPECT_FALSE(read_ended);
}

// ── InboundAtLimitAccepted (T055; moved from test_070_max_message_size_test) ──
//
// Re-based through the pump: a configured 383 inside [4096, 262144] and a Heartbeat
// padded to exactly that many bytes, fed through the Framer, is delivered and the
// session stays Active.
TEST(EngineReadPumpResync, InboundAtLimitAccepted) {
    constexpr std::uint32_t kAdvertised = 5000;
    ResyncCell c{kAdvertised};
    std::string const f =
        c.up ? c.rig.msg_of_size("0", 2, {}, kAdvertised, "58", false) : std::string{};
    bool const d = !f.empty() && c.rig.deliver(f);
    bool const delivered = d && c.wait_next_in(3);
    auto const st = c.rig.state();
    c.rig.stop();

    ASSERT_TRUE(c.up && d) << "setup";
    EXPECT_EQ(f.size(), kAdvertised);
    EXPECT_TRUE(delivered) << "a frame of exactly the advertised size is delivered";
    EXPECT_EQ(st, fsm_state::Active);
}
