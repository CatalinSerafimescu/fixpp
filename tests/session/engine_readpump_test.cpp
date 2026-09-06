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
// Anti-hang: every coroutine carries a self-deadline steady_timer.
//            All ioc.run_for() calls are explicitly bounded.
//
// Observability:
//   SeqnumManager::next_inbound_unsafe() (exposed via FIXPP_TEST_HOOKS).
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
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "engine_loopback_harness.hpp"
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
// ⚠️ FOUR OF THIS FILE'S NEW SITES SIT IN `if (!acc)` EARLY-EXIT BRANCHES that do not
// execute on a healthy box, so `ci/pump-seam-arm.sh` reports them NO-SUCH-SITE. That is
// the driver being honest about an unreached site, not a defect -- and it is a reason to
// read a NO-SUCH-SITE before believing it names a missing label.
//
// The drain is the CLOCKED one, spelled `*h->engine->clock()`. `build_harness` above
// installs a real `system_clock_source` into the `EngineConfig` and `std::move`s that
// config into the engine, so the accessor is the only live spelling; non-nullness
// rests on that assignment, not on the accessor's "never null post-construction"
// comment, which is #289's standing known-false one.
#include "transport/loopback_tls_fixture.hpp"

using namespace std::chrono_literals;
using fixpp::session::fsm_state;

namespace {

// ── Frame builder helpers ─────────────────────────────────────────────────────

// 041 T019: a real engine clock activates the session SendingTime(52) MaxLatency
// guard (inert under the prior null clock). Inbound frames feeding a live session
// must carry a fresh 52 or the guard rejects them (reason=10). Mirrors the 038/US3
// fix in engine_acceptor_test.cpp.
static std::string utc_now_fix_timestamp() {
    std::array<char, 32> buf{};
    auto r = fixpp::core::utc_time_to_fix_string(std::chrono::system_clock::now(),
                                                 fixpp::core::fix_time_precision::millis,
                                                 std::span<char>{buf});
    return r ? std::string{r->data(), r->size()} : std::string{};
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_str, std::string_view msg_type,
                                             int seq_num, std::string_view sender,
                                             std::string_view target, std::string extra_body = "") {
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
    cs &= 0xFFu;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> out;
    out.reserve(msg.size());
    for (char c : msg) out.push_back(static_cast<std::byte>(c));
    return out;
}

static std::vector<std::byte> make_logon_frame(std::string_view begin_str, std::string_view sender,
                                               std::string_view target) {
    return make_fix_frame(begin_str, "A", 1, sender, target,
                          "98=0\x01"
                          "108=30\x01");
}

static std::vector<std::byte> make_heartbeat_frame(std::string_view begin_str, int seq_num,
                                                   std::string_view sender,
                                                   std::string_view target) {
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

static std::unique_ptr<ReadPumpHarness> build_harness(asio::io_context& ioc) {
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

static asio::awaitable<void> run_client_with_extra_frames(
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
    ASSERT_NE(port, 0u) << "acceptor listener did not bind";

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
    const auto next_inbound = static_cast<int>(acc->seqnum_mgr_test_access().next_inbound_unsafe());
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
    ASSERT_NE(port, 0u) << "acceptor listener did not bind";

    // Build an oversized frame: body is >64 KiB of repeated 'X'.
    // This clearly exceeds kReadPumpCarryCapacity (64 KiB), so the framer
    // will return wire_frame_too_large before any frame bytes reach on_inbound_frame.
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
    const auto next_inbound = static_cast<int>(acc->seqnum_mgr_test_access().next_inbound_unsafe());

    auto stop_fut = asio::co_spawn(ioc, h->engine->stop(), asio::use_future);
    if (!fixpp::test_support::run_to_exhaustion_or_report(
            ioc, stop_fut, "EngineReadPumpTest::OverCapacityFrameClosesSession/stop_fut2")) {
        return;
    }
    stop_fut.get();

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
    ASSERT_NE(port, 0u) << "acceptor listener did not bind";

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
    ASSERT_NE(port, 0u) << "acceptor listener did not bind";

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
    ASSERT_NE(port, 0u) << "acceptor listener did not bind";

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
