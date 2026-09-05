// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_reconnect_happy_path.cpp — T014 [US1] Phase 3 RED
//
// Initiator reconnect after transient disconnect:
//   FSM destroys dead Transport → walks ReconnectPolicy schedule →
//   mints fresh Transport via TransportFactory::make(...) per attempt →
//   re-Logon → issues ResendRequest{BeginSeqNo, EndSeqNo} for missing-inbound
//   range → receives replayed messages → reaches Active (no operator).
//
// Anchors: spec.md §US1 AC1 / FR-001 / FR-002 / FR-009; data-model.md §E-1;
//   plan.md §Test plan T014; [FIX-SL §4.3.2]; contracts/reconnect_fsm.hpp;
//   transport_factory.hpp (make signature).
//
// Production-shape:
//   T014-A/B: drives bytes through Session::on_inbound_frame() (NOT internal
//     helper calls) per [[project_005_phase8_completeness_false_pass]].
//   T014-D: uses a hand-rolled TestTransportFactory wired via
//     SessionConfig::transport_factory_override; witnesses TransportFactory::make
//     call-count to confirm drive_reconnect_attempt invokes the factory per
//     FR-001 / FR-002 ("mint fresh Transport per attempt").
//
// RED witness (all 3 behavioral cells):
//   T014-A: ReconnectFsm::enter_awaiting_resend stub returns {} without setting
//     awaiting_resend_ or emitting ResendRequest(2) → FAILS RED.
//   T014-B: session stays Disconnected (005 fatal path) → FAILS RED.
//   T014-D: drive_reconnect_attempt stub returns {} without calling factory_->make
//     → make_call_count stays 0, never reaches 2 → FAILS RED.
//
// T014-C is a link-time smoke (always GREEN — no behavioral assertion).

#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/reconnect_fsm.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// mock_transport is a test-only header; gate it with the required define.
#define FIXPP_ALLOW_MOCK_TRANSPORT
#include <fixpp/transport/test/mock_transport.hpp>

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
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

using namespace std::chrono_literals;

namespace {

// ── FIX frame builder helpers ─────────────────────────────────────────────────

static std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view extra = {}) {
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    if (!extra.empty()) body += std::string(extra);

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) cs += c;
    cs &= 0xFFU;
    char csbuf[5];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

static std::vector<std::byte> make_heartbeat(std::string_view bs, std::uint32_t seq,
                                             std::string_view s, std::string_view t) {
    return make_fix_frame(bs, "0", seq, s, t);
}

// Outbound frame collector — installed as SessionConfig::transport_send.
struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

// Check if a frame is of a given MsgType (tag 35).
static bool is_msg_type(std::span<const std::byte> frame, std::string_view type) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = "35=" + std::string(type) + "\x01";
    return wire.find(needle) != std::string::npos;
}

static bool is_resend_request(std::span<const std::byte> frame) { return is_msg_type(frame, "2"); }

}  // namespace

// ── Test fixture ──────────────────────────────────────────────────────────────

class ReconnectHappyPathTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    OutboundCapture capture;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_acceptor_cfg() {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role = fixpp::session::session_role::acceptor;
        // RC#C (gate-b/r1): bilateral_lenient — tests here don't exercise reset semantics.
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 100ms,
                                                        "ReconnectHappyPathTest::run_open")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "ReconnectHappyPathTest::run_open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ReconnectHappyPathTest::run_open";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 100ms,
                                                        "ReconnectHappyPathTest::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "ReconnectHappyPathTest::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ReconnectHappyPathTest::feed";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    bool drive_to_active(fixpp::session::Session& s) {
        auto r = run_open(s);
        if (!r.has_value()) return false;
        auto logon = make_logon("FIX.4.2", 1, "TW", "ISLD");
        feed(s, logon);
        return s.state() == fixpp::session::fsm_state::Active;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// T014-A: On inbound MsgSeqNum > next_expected, session enters AwaitingResend
//   (awaiting_resend flag TRUE on the ReconnectFsm).
//
// RED: ReconnectFsm::enter_awaiting_resend stub returns {} and does NOT set
//   awaiting_resend_ = true. EXPECT_TRUE(fsm.is_awaiting_resend()) FAILS.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectHappyPathTest, TooHighInboundSeqEntersAwaitingResend) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess)) << "Precondition: session must reach Active state";

    // After Logon(seq=1) consumed, next expected = 2.
    // Inject Heartbeat with seq=5 (gap [2..4]).
    auto hb = make_heartbeat("FIX.4.2", 5, "TW", "ISLD");
    feed(sess, hb);

    // The session's reconnect FSM should now be in AwaitingResend.
    // The ReconnectFsm is embedded inside session; we cannot access it directly
    // from the public API. We verify the OBSERVABLE consequence: the session
    // must emit a ResendRequest(2) outbound before or after the gap message.
    //
    // RED assertion: no ResendRequest in capture.frames (stub doesn't emit one).
    bool found_resend_request = false;
    for (const auto& frame : capture.frames) {
        if (is_resend_request(frame)) {
            found_resend_request = true;
            break;
        }
    }
    EXPECT_TRUE(found_resend_request)
        << "Expected a ResendRequest(MsgType=2) outbound after too-high inbound "
        << "MsgSeqNum. The ReconnectFsm::enter_awaiting_resend stub does not "
        << "emit ResendRequest yet — this assertion FAILS RED per T014 design.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T014-B: After gap closes (peer sends all missing msgs), session returns to
//   Active with awaiting_resend = false.
//
// RED: Not even the gap detection fires yet, so this test confirms the
//   state after replay — which we cannot reach until T023 impl lands.
//   Verifies Active state is maintained on valid-seqnum continuation.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectHappyPathTest, GapCloseRestoresActiveState) {
    auto cfg = make_acceptor_cfg();
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(drive_to_active(sess));

    // Inject gap message (seq=5 when expected=2).
    auto hb_gap = make_heartbeat("FIX.4.2", 5, "TW", "ISLD");
    feed(sess, hb_gap);

    // Inject fill messages seq=2, 3, 4 (PossDupFlag=Y would be set in real
    // replay but the key is the sequence numbers).
    for (std::uint32_t seq = 2; seq <= 4; ++seq) {
        auto hb_fill = make_heartbeat("FIX.4.2", seq, "TW", "ISLD");
        feed(sess, hb_fill);
    }

    // After gap fills, session should be Active (not Disconnected).
    // RED: the stub doesn't enter AwaitingResend so the session may have gone
    //   Disconnected on the too-high detection or stayed in some wrong state.
    // We assert Active to detect the gap between stub and real impl.
    // If the session went Disconnected (fatal behavior of old 005 stub), this FAILS.
    EXPECT_EQ(sess.state(), fixpp::session::fsm_state::Active)
        << "After gap-fill messages seq=2..4, session should return to Active. "
        << "RED: the 005 stub treats too-high as fatal → Disconnected. "
        << "This assertion documents the T014 behavioral delta.";
}

// ─────────────────────────────────────────────────────────────────────────────
// T014-C: ReconnectFsm accessors compile and are callable (link-time smoke).
//   is_awaiting_resend() and current_resend_state() must be accessible.
//
// RED/GREEN: This cell compiles and PASSES because the stubs return {} / false.
//   The behavioral assertion above (T014-A) is the true RED cell.
// ─────────────────────────────────────────────────────────────────────────────
TEST_F(ReconnectHappyPathTest, ReconnectFsmAccessorsCompile) {
    // Build a minimal ReconnectFsm directly to verify the public API compiles.
    // Non-owning raw pointer — no factory needed for this API smoke test.
    using fixpp::session::ReconnectFsm;
    using fixpp::transport::ReconnectPolicy;

    // ReconnectFsm ctor requires a factory pointer. Use null — not dereferenced
    // by is_awaiting_resend() or current_resend_state() (stub impl).
    ReconnectPolicy policy;
    policy.max_attempts = 3;
    policy.schedule = {std::chrono::milliseconds{1000}, std::chrono::milliseconds{2000},
                       std::chrono::milliseconds{4000}};

    ReconnectFsm fsm(nullptr, std::move(policy), std::chrono::seconds{30}, 2000ms);

    EXPECT_FALSE(fsm.is_awaiting_resend())
        << "Default-constructed FSM must not be in AwaitingResend";

    // current_resend_state() is only valid when is_awaiting_resend() == true,
    // but we verify it doesn't crash on the default-constructed state.
    [[maybe_unused]] const auto& rs = fsm.current_resend_state();
    EXPECT_EQ(rs.outstanding_begin, 0u);
    EXPECT_EQ(rs.outstanding_end, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// T014-D: drive_reconnect_attempt() mints a fresh Transport via
//   TransportFactory::make() per FR-001 / FR-002 ("mint fresh Transport per
//   reconnect attempt"). Witnesses the FR-001/FR-002 behavioral contract that
//   T014-A/B/C do NOT cover (those test FR-009 gap-detection, not the
//   reconnect loop itself). Anchors: FR-001, FR-002, plan.md T014.
//
// RED witness: the Phase 2 stub in reconnect_fsm.cpp returns co_return {} at
//   line 46 WITHOUT calling factory_->make(). make_call_count stays 0 after
//   one drive_reconnect_attempt() call, never reaching 1 → EXPECT_GE(1) FAILS.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Hand-rolled TestTransportFactory — 20 lines, anonymous namespace.
// make() records invocation and returns a scripted mock_transport whose
// inbound_bytes buffer is empty (EOF immediately) to keep the test short.
// reload_credentials() is a no-op success per FR-033 contract.
// Anchors: transport_factory.hpp make() + reload_credentials() signatures.
class TestTransportFactory final : public fixpp::transport::TransportFactory {
public:
    std::atomic<int> make_call_count{0};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor exec, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        ++make_call_count;
        fixpp::transport::test::Script script;
        // Empty inbound → mock delivers EOF immediately on first read.
        return std::make_unique<fixpp::transport::test::mock_transport>(std::move(exec),
                                                                        std::move(script));
    }

    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*new_source*/) noexcept override {
        return {};
    }

    // 014 T004 — C4: cert_source_snapshot() promoted to pure-virtual on the
    // abstract TransportFactory base; trivial override returns nullptr (this
    // factory holds no cert_source; only make() call-count is tested here).
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }
};

}  // namespace

TEST_F(ReconnectHappyPathTest, ReconnectLoopMintsFreshTransport) {
    // Wire the TestTransportFactory into a ReconnectFsm directly.
    // The FSM holds a non-owning raw pointer (factory_ member); the shared_ptr
    // here acts as the owning anchor matching the SessionConfig precedent.
    auto factory_owner = std::make_shared<TestTransportFactory>();

    using fixpp::session::ReconnectFsm;
    using fixpp::transport::ReconnectPolicy;

    ReconnectPolicy policy;
    policy.max_attempts = 3;
    policy.schedule = {10ms, 20ms, 40ms};

    // Pass raw pointer — non-owning, factory_owner is the strong ref anchor.
    ReconnectFsm fsm(factory_owner.get(), std::move(policy), std::chrono::seconds{30}, 2000ms);

    // drive_reconnect_attempt() MUST call factory_->make(...) once per attempt
    // (FR-001 / FR-002). The Phase 2 stub returns {} without calling make() →
    // make_call_count stays 0 after the call → EXPECT_GE(1) FAILS RED.
    auto fut = asio::co_spawn(ioc, fsm.drive_reconnect_attempt(), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();

    // Consume the result (ignore error variant — stub returns success).
    (void)fut.get();

    EXPECT_GE(factory_owner->make_call_count.load(), 1)
        << "drive_reconnect_attempt() must call TransportFactory::make() at "
        << "least once per FR-001 / FR-002 (mint fresh Transport per attempt). "
        << "RED: Phase 2 stub returns {} without invoking make(). "
        << "make_call_count=" << factory_owner->make_call_count.load() << " expected >= 1.";
}
