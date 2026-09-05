// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_070_posture_mismatch_test.cpp
//
// 070-fix44-closeout US1 (S-029) — TestMessageIndicator(464) posture-mismatch refusal.
//
// Discriminating witness (FR-002/FR-003/FR-012, tasks.md T006):
//   (a) posture=production + inbound Logon 464=Y      → Disconnected (refused, not Active).
//   (b) posture=test       + inbound Logon 464=N      → Disconnected;
//       posture=test       + inbound Logon (no 464)   → Disconnected.
//   (c) posture=production + 464=N / absent           → Active (no false reject).
//   (d) posture UNSET (default) + 464=Y               → Active (opt-in; baseline unchanged).
//   (e) posture=production + malformed 464=foo        → Disconnected (refused-as-malformed).
//   + advertise: build_logon with opts.test_message_indicator ⇒ 464=Y on the wire; else absent.
//
// The (a) vs (d) pair is the discriminator: the SAME inbound frame (464=Y) is refused
// when posture is enforced and accepted when it is not — proving the new path fires
// only under configuration.
#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstdio>
#include <fixpp/core/clock.hpp>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/store_double.hpp"

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

namespace fixpp::session::test {
namespace {

using fixpp::session::fsm_state;

// ── Gate B PR #189 FQ-4 — StoreDouble-backed persistent store factory ─────────
//
// Minimal duplication of the tests/session/test_reset_on_lifecycle.cpp
// SharedStoreWrapper/StoreDoubleFactory pattern (internal linkage via the
// enclosing anonymous namespace — no ODR clash across translation units).
// Gives the test a shared_ptr<StoreDouble> so seed_outbound() can establish a
// durable next-outbound value BEFORE the acceptor's Logon-branch hydration
// runs, distinguishing "the refusal Logout's seq came from the durable store"
// from "the un-hydrated construction default" (the FQ-3 regression this
// witness pins).
class SharedStoreWrapper final : public MessageStore {
public:
    explicit SharedStoreWrapper(std::shared_ptr<StoreDouble> delegate) noexcept
        : MessageStore(flush_thunk_for<SharedStoreWrapper>()), delegate_(std::move(delegate)) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        return delegate_->store(seq, frame, dir);
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor) noexcept override {
        return delegate_->retrieve(begin, end, dir, visitor);
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        return delegate_->next_seqnum(dir, increment);
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        return delegate_->reset();
    }

private:
    std::shared_ptr<StoreDouble> delegate_;
};

class StoreDoubleFactory final : public MessageStoreFactory {
public:
    std::shared_ptr<StoreDouble> store = std::make_shared<StoreDouble>();

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<SharedStoreWrapper>(store);
    }
};

// Build an inbound Logon frame, optionally carrying TestMessageIndicator(464).
std::vector<std::byte> make_logon_frame_464(std::string_view begin_string,
                                            std::uint32_t msg_seq_num,
                                            std::string_view sender_comp_id,
                                            std::string_view target_comp_id, int heartbt_int,
                                            std::optional<std::string_view> tmi_464) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(msg_seq_num) + "\x01";
    body += "49=" + std::string(sender_comp_id) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target_comp_id) + "\x01";
    body += "98=0\x01";
    body += "108=" + std::to_string(heartbt_int) + "\x01";
    if (tmi_464.has_value()) {
        body += "464=" + std::string(*tmi_464) + "\x01";
    }

    std::string full = "8=" + std::string(begin_string) + "\x01";
    full += "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) {
        cs += c;
    }
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

std::string extract_field(std::span<const std::byte> frame, int tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    return end == std::string::npos ? std::string{} : wire.substr(pos, end - pos);
}

class PostureTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    // Gate B PR #189 FQ-4: all outbound frames captured by transport_send, so a
    // test can assert the NAMED FR-002 postcondition (Logout(35=5)+58=<text>)
    // directly rather than the fsm_state::Disconnected proxy.
    std::vector<std::vector<std::byte>> captured_frames;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Acceptor (sender=ISLD, target=TW). posture nullopt ⇒ enforcement disabled.
    fixpp::session::SessionConfig make_acceptor_cfg(
        std::optional<fixpp::session::session_posture> posture) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.posture = posture;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    // Initiator (sender=TW, target=ISLD) — exercises the S-029 initiator arm
    // (session.cpp ~3776-3783), untested before Gate B PR #189 FQ-4.
    fixpp::session::SessionConfig make_initiator_cfg(
        std::optional<fixpp::session::session_posture> posture) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "TW";
        cfg.target_comp_id = "ISLD";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = std::chrono::seconds{30};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = fixpp::session::session_role::initiator;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.posture = posture;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    fixpp::core::expected_t<void> open_sync(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "PostureTest::open_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "PostureTest::open_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "PostureTest::open_sync";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    void feed_sync(fixpp::session::Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "PostureTest::feed_sync")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "PostureTest::feed_sync");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "PostureTest::feed_sync";
            return;
        }
        (void)fut.get();  // refusal is a "successful" processing that ends Disconnected
    }

    // Drive an acceptor with the given posture + inbound 464 value; return the FSM state.
    fsm_state run(std::optional<fixpp::session::session_posture> posture,
                  std::optional<std::string_view> tmi_464) {
        auto cfg = make_acceptor_cfg(posture);
        fixpp::session::Session sess(engine, cfg);
        EXPECT_TRUE(open_sync(sess).has_value());
        auto frame = make_logon_frame_464("FIX.4.4", 1, "TW", "ISLD", 30, tmi_464);
        feed_sync(sess, std::span<const std::byte>{frame});
        return sess.state();
    }
};

// (a) production posture refuses a test-marked peer. Gate B PR #189 FQ-4:
// assert the NAMED FR-002 postcondition — exactly one outbound frame, a
// Logout(35=5) carrying the exact posture-mismatch text — not just the
// terminal fsm_state proxy (a silent disconnect, missing/empty 58, or wrong
// disposition would all have passed the state-only assertion alone).
TEST_F(PostureTest, ProductionRefusesTestPeer) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::string_view{"Y"}),
              fsm_state::Disconnected);
    ASSERT_EQ(captured_frames.size(), 1u)
        << "exactly one outbound frame (the refusal Logout) — no partial/extra emit";
    const auto& lo = captured_frames.front();
    EXPECT_EQ(extract_field(lo, 35), "5") << "refusal disposition must be Logout(35=5)";
    EXPECT_EQ(extract_field(lo, 58), "TestMessageIndicator posture mismatch");
}

// (b) test posture refuses a production peer — explicit 464=N and absent-464.
TEST_F(PostureTest, TestRefusesProductionPeer_464N) {
    EXPECT_EQ(run(fixpp::session::session_posture::test, std::string_view{"N"}),
              fsm_state::Disconnected);
}
TEST_F(PostureTest, TestRefusesProductionPeer_Absent) {
    EXPECT_EQ(run(fixpp::session::session_posture::test, std::nullopt), fsm_state::Disconnected);
}

// (c) production posture accepts a production peer — no false reject.
TEST_F(PostureTest, ProductionAcceptsProductionPeer_464N) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::string_view{"N"}),
              fsm_state::Active);
}
TEST_F(PostureTest, ProductionAcceptsProductionPeer_Absent) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::nullopt), fsm_state::Active);
}

// (d) opt-in proof: posture UNSET accepts the SAME 464=Y frame that (a) refused.
TEST_F(PostureTest, UnsetPostureIgnores464) {
    EXPECT_EQ(run(std::nullopt, std::string_view{"Y"}), fsm_state::Active);
}

// (e) malformed 464 (not Y/N) is refused.
TEST_F(PostureTest, ProductionRefusesMalformed464) {
    EXPECT_EQ(run(fixpp::session::session_posture::production, std::string_view{"foo"}),
              fsm_state::Disconnected);
}

// Gate B PR #189 FQ-4 — initiator-role posture-mismatch witness. The initiator
// arm (session.cpp ~3776-3783) fires on the peer's inbound Logon-ack; it was
// completely uncovered before this change. Same discriminating assertions as
// (a) above: exactly one NEW outbound frame (the refusal Logout) with the
// named 35=5/58=<text> shape, plus Disconnected.
TEST_F(PostureTest, InitiatorRefusesPostureMismatchedLogonAck) {
    auto cfg = make_initiator_cfg(fixpp::session::session_posture::production);
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);
    const auto frames_before_ack = captured_frames.size();  // the initial outbound Logon

    // Peer (acceptor ISLD→TW) sends a test-marked Logon-ack while we run production.
    auto ack = make_logon_frame_464("FIX.4.4", 1, "ISLD", "TW", 30, std::string_view{"Y"});
    feed_sync(sess, std::span<const std::byte>{ack});

    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
    ASSERT_EQ(captured_frames.size(), frames_before_ack + 1)
        << "exactly one NEW outbound frame (the refusal Logout)";
    const auto& lo = captured_frames.back();
    EXPECT_EQ(extract_field(lo, 35), "5") << "refusal disposition must be Logout(35=5)";
    EXPECT_EQ(extract_field(lo, 58), "TestMessageIndicator posture mismatch");
}

// Gate B PR #189 New P2 / FQ-3 — persistent-store seqnum witness. Seeds a
// durable next-outbound N>1 into the store BEFORE the acceptor's Logon-branch
// hydration runs, then asserts the refusal Logout carries 34=N — NOT the
// un-hydrated construction default (seqnum_min=1). This is RED against the
// pre-FQ-3 placement (posture check ran before ensure_hydrated_, so
// peek_outbound() returned 1 regardless of the seeded store) and GREEN after
// (FQ-3 relocates the posture check to after ensure_hydrated_).
TEST_F(PostureTest, AcceptorRefusalUsesDurableOutboundSeq) {
    constexpr fixpp::session::seqnum_t kDurableNextOutbound = 7;
    auto factory = std::make_shared<StoreDoubleFactory>();
    factory->store->seed_outbound(kDurableNextOutbound);

    auto cfg = make_acceptor_cfg(fixpp::session::session_posture::production);
    cfg.store_factory = factory;
    fixpp::session::Session sess(engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());

    auto frame = make_logon_frame_464("FIX.4.4", 1, "TW", "ISLD", 30, std::string_view{"Y"});
    feed_sync(sess, std::span<const std::byte>{frame});

    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
    ASSERT_EQ(captured_frames.size(), 1u);
    const auto& lo = captured_frames.front();
    EXPECT_EQ(extract_field(lo, 35), "5") << "refusal disposition must be Logout(35=5)";
    EXPECT_EQ(extract_field(lo, 34), std::to_string(kDurableNextOutbound))
        << "refusal Logout must carry the durable next-outbound seq (peek_outbound() "
           "post-hydration), not the un-hydrated construction default";
}

// Gate B PR #189 FQ-5 — null-clock refusal witness. EngineConfig::clock is
// left unset (nullptr default per engine_config.hpp:127); Session::open()
// only RESOLVES effective_clock_ = cfg_.clock_override ?: engine_.clock
// (session.cpp:1160-1165) — validate_engine_config()'s clock_not_set gate is
// an Engine::open()-level precondition this direct-Session harness never
// calls, so effective_clock_ == nullptr reaches refuse_logon_with_logout_.
// Pre-fix, that helper unconditionally dereferenced *effective_clock_ →
// crash; post-fix it degrades to an empty SendingTimeStamp (mirrors the
// guarded ternary at session.cpp ~L3034) and the refusal completes normally.
TEST_F(PostureTest, RefusalSurvivesNullClock) {
    fixpp::core::EngineConfig no_clock_engine{};
    no_clock_engine.executor = ioc.get_executor();
    // no_clock_engine.clock intentionally left null.

    auto cfg = make_acceptor_cfg(fixpp::session::session_posture::production);
    fixpp::session::Session sess(no_clock_engine, cfg);
    ASSERT_TRUE(open_sync(sess).has_value());

    auto frame = make_logon_frame_464("FIX.4.4", 1, "TW", "ISLD", 30, std::string_view{"Y"});
    feed_sync(sess, std::span<const std::byte>{frame});

    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
    ASSERT_EQ(captured_frames.size(), 1u)
        << "exactly one outbound frame (the refusal Logout) — no crash, no partial emit";
    const auto& lo = captured_frames.front();
    EXPECT_EQ(extract_field(lo, 35), "5") << "refusal disposition must be Logout(35=5)";
    EXPECT_EQ(extract_field(lo, 58), "TestMessageIndicator posture mismatch");
    EXPECT_EQ(extract_field(lo, 52), "") << "degraded/empty SendingTime under a null clock";
}

// Advertise side: build_logon emits 464=Y iff opts.test_message_indicator.
TEST(PostureAdvertise, BuildLogonEmits464YWhenTest) {
    std::array<std::byte, 512> buf{};
    auto with_flag = fixpp::session::build_logon(
        std::span<std::byte>{buf.data(), buf.size()}, 1, "TW", "ISLD", "FIX.4.4", 30,
        "20240101-00:00:00.000", false, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{.test_message_indicator = true});
    ASSERT_TRUE(with_flag.has_value());
    EXPECT_EQ(extract_field(*with_flag, 464), "Y");

    std::array<std::byte, 512> buf2{};
    auto without_flag = fixpp::session::build_logon(
        std::span<std::byte>{buf2.data(), buf2.size()}, 1, "TW", "ISLD", "FIX.4.4", 30,
        "20240101-00:00:00.000", false, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
        fixpp::session::logon_advertise_options{});
    ASSERT_TRUE(without_flag.has_value());
    EXPECT_EQ(extract_field(*without_flag, 464), "") << "no 464 when flag false (byte-identical)";
}

}  // namespace
}  // namespace fixpp::session::test
