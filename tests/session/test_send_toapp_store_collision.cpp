// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_send_toapp_store_collision.cpp
//
// 059-outbound-store-fail-closed — gate-b/r2 FQ-1 RED witness.
//
// Round-2 Codex P1: Session::send's disconnect guard classified by error
// VALUE (`is_persistent_retain_fatal(impl_r.error())`, range [56,65)).
// `Application::toApp` may return `unexpected(<any error>)` — including a
// store-block code — from `send_impl`'s toApp veto/other-error return
// (`:4499`, "Session stays Active (INV-5/SC-004)"), BEFORE assign_outbound()
// or store_then_emit ever run: no seqnum consumed, nothing stored, nothing
// transmitted. The pre-fix value-based guard cannot distinguish that
// passthrough from a genuine commit-region store failure carrying the same
// error value, and spuriously disconnects — a PR-introduced regression
// against INV-5/SC-004 (opus_pr163_2_triage.md P1).
//
// This witness drives a live Session (persistent store double —
// yields_persistent_store() defaults true per MessageStoreFactory) through
// a toApp that returns unexpected(store_io_failure), and asserts:
//   - send() returns that EXACT error un-coerced,
//   - no transmit (wire frame count unchanged),
//   - no seqnum consumption (outbound counter unchanged),
//   - session stays Active.
// RED on the pre-fix tree (session Disconnects); GREEN after gate-b/r2 FQ-1
// (send_impl's disconnect_required out-param, set ONLY at the two
// commit-region producer sites). Authored fresh — does not widen
// test_application_outbound.cpp's ToAppOtherErrorAbortsWithThatError
// (which uses session_invalid_argument, outside [56,65), so it never
// exercised this collision).
//
// Anchors: research/reviews/opus_pr163_2_triage.md FQ-1;
// contracts/store-then-emit-disposition.md item 3;
// [[feedback_fail_placeholder_red_test]].
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::Session;
using fixpp::session::SessionId;
using fixpp::session::session_role;
using fixpp::session::seqnum_t;
using fixpp::wire::access_mode;
using fixpp::wire::MessageView;

// ── Frame-building helpers (mirror test_store_fail_reconcile_breadth.cpp) ───

std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

std::vector<std::byte> make_fix_frame(std::string_view begin_string, std::string_view msg_type,
                                      std::uint32_t seq, std::string_view sender,
                                      std::string_view target, std::string_view extra = {}) {
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

std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                  std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

std::vector<std::byte> make_app_payload(std::string_view clordid) {
    std::string body = "35=D\x01" + std::string(field(11, clordid)) + "54=1\x01"
                                                                       "55=AAPL\x01";
    std::vector<std::byte> v;
    v.reserve(body.size());
    for (char c : body) v.push_back(static_cast<std::byte>(c));
    return v;
}

// ── PlainPersistentStore: a minimal PERSISTENT store double that never
//    fails — this witness collides on the toApp VALUE, not a genuine store
//    failure, so no fault-injection seam is needed. yields_persistent_store()
//    is NOT overridden by PlainPersistentStoreFactory — MessageStoreFactory's
//    default is true (persistent), exactly as research.md D6 describes. ────
class PlainPersistentStore final : public MessageStore {
public:
    PlainPersistentStore() : MessageStore(flush_thunk_for<PlainPersistentStore>()) {}

    [[nodiscard]] asio::awaitable<expected_t<void>> store(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/, direction_t /*dir*/) noexcept override {
        co_return expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<expected_t<void>> retrieve(
        seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override {
        co_return expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        auto& c = (dir == direction_t::outbound) ? next_out_ : next_in_;
        const seqnum_t cur = c;
        if (increment) ++c;
        co_return cur;
    }

    [[nodiscard]] asio::awaitable<expected_t<void>> reset() noexcept override {
        next_in_ = next_out_ = 1;
        co_return expected_t<void>{};
    }

private:
    seqnum_t next_in_ = 1;
    seqnum_t next_out_ = 1;
};

class PlainPersistentStoreFactory final : public MessageStoreFactory {
public:
    // NOTE: no yields_persistent_store() override — defaults to true (persistent).
    [[nodiscard]] expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::make_unique<PlainPersistentStore>();
    }
};

// ── TrackingApplication: returns a configurable error from toApp ────────────
// (mirrors test_application_outbound.cpp's TrackingApplication).

class TrackingApplication : public Application {
public:
    bool to_app_veto = false;
    error to_app_error = error::app_do_not_send;
    int to_app_calls = 0;

    expected_t<void> toApp(const MessageView<access_mode::Index>& /*msg*/,
                           const SessionId& /*id*/) override {
        ++to_app_calls;
        if (to_app_veto) {
            return std::unexpected(to_app_error);
        }
        return {};
    }

    void toAdmin(const MessageView<access_mode::Index>& /*msg*/, const SessionId& /*id*/) override {}
};

// ── Fixture ───────────────────────────────────────────────────────────────

class SendToAppStoreCollisionTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }
};

// ─────────────────────────────────────────────────────────────────────────
// gate-b/r2 FQ-1: a toApp veto carrying a store-block error VALUE on a
// PERSISTENT store must stay Active — the collision is in the disconnect
// DECISION, not in whether the value is store-shaped. RED pre-fix (session
// spuriously Disconnects); GREEN post-fix.
// ─────────────────────────────────────────────────────────────────────────
TEST_F(SendToAppStoreCollisionTest,
       ToAppStoreBlockValuedError_PersistentStore_StaysActive_NoTransmit_NoSeqnumConsumed) {
    auto app = std::make_shared<TrackingApplication>();
    app->to_app_veto = true;
    app->to_app_error = error::store_io_failure;  // a store-block value, NOT via the commit path
    engine.application = app;

    auto factory = std::make_shared<PlainPersistentStoreFactory>();

    fixpp::session::SessionConfig cfg;
    cfg.role = session_role::initiator;
    cfg.sender_comp_id = "INITR";
    cfg.target_comp_id = "ACCEPTR";
    cfg.begin_string = "FIX.4.2";
    cfg.heartbeat_interval = 0s;
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.store_factory = factory;

    std::vector<std::vector<std::byte>> wire;
    cfg.transport_send = [&](std::span<const std::byte> f) { wire.emplace_back(f.begin(), f.end()); };

    Session sess(engine, cfg);

    auto open_r = asio::co_spawn(ioc, sess.open(), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, open_r, 200ms,
                                                    "ToAppStoreBlockValuedError/open")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                        "ToAppStoreBlockValuedError/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ToAppStoreBlockValuedError/open";
        return;
    }
    ASSERT_TRUE(open_r.get().has_value()) << "open() must succeed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    auto peer_logon = make_logon("FIX.4.2", 1, "ACCEPTR", "INITR");
    auto logon_r = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>(peer_logon)),
                                  asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, logon_r, 200ms,
                                                    "ToAppStoreBlockValuedError/logon-ack")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                        "ToAppStoreBlockValuedError/logon-ack");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ToAppStoreBlockValuedError/logon-ack";
        return;
    }
    ASSERT_TRUE(logon_r.get().has_value()) << "peer Logon-ack must be accepted";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const std::size_t frames_before = wire.size();
    const seqnum_t outbound_before = sess.seqnum_mgr_test_access().peek_outbound();

    auto payload = make_app_payload("ORD1");
    auto send_fut = asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    if (!fixpp::test_support::run_window_then_ready(ioc, send_fut, 200ms,
                                                    "ToAppStoreBlockValuedError")) {
        fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "ToAppStoreBlockValuedError");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "ToAppStoreBlockValuedError";
        return;
    }
    auto send_r = send_fut.get();

    ASSERT_EQ(app->to_app_calls, 1) << "toApp must have fired for the collision to be exercised";

    // The exact toApp error, un-coerced (gate-b/r2 FQ-1 design constraint 1).
    ASSERT_FALSE(send_r.has_value()) << "send() must return the toApp error";
    EXPECT_EQ(send_r.error(), error::store_io_failure)
        << "the toApp error must propagate un-coerced, not be normalized";

    // No transmit — the toApp veto fires before assign_outbound()/store_then_emit.
    EXPECT_EQ(wire.size(), frames_before) << "a toApp-vetoed send must NOT cross wire";

    // No seqnum consumption — assign_outbound() runs AFTER the toApp check.
    EXPECT_EQ(sess.seqnum_mgr_test_access().peek_outbound(), outbound_before)
        << "a toApp-vetoed send must NOT consume an outbound seqnum";

    // The load-bearing assertion (gate-b/r2 FQ-1 P1): the session must stay
    // Active. Pre-fix, the value-based guard (is_persistent_retain_fatal on
    // the RETURNED value) spuriously disconnects here because
    // store_io_failure falls in [56,65) — even though send_impl never
    // reached the commit region.
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "gate-b/r2 FQ-1 (INV-5/SC-004): a toApp error carrying a store-block VALUE must "
           "stay Active when it never reached send_impl's commit region";
}

}  // namespace
