// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_validation_compat_toggles.cpp
//
// 028-validation-compat-toggles unit test suite.
//
// Phase 1 (Setup) — T001: skeleton compiles and registers.
// Real witnesses (T004/T006/T007/T012) are added in later phases.
//
// Fixture shape mirrors test_next_expected_msgseqnum.cpp:
//   struct OutboundCapture — captures outbound frames via transport_send_.
//   class CountingApp028 — Application subclass recording callback invocations.
//   struct Fixture — io_context + SessionConfig + Session under test.
//   make_acceptor / make_initiator — helpers that build Active / LogonSent sessions.
//
// Tests are free TEST(...) macros (not TEST_F) per tasks.md T001 convention.
//
// Anchors: spec.md FR-001..013, SC-001..008; data-model.md I-VCT-1..11;
//          contracts/validation-compat-toggles.md C1/C2/C3.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/compid_authorization_policy.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>

#include "support/identity_injecting_transport.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

// mallocnesia weak-symbol hooks — replaced by LD_PRELOAD; no-ops otherwise.
// Must be at file scope for the LD_PRELOAD override to bind.
extern "C" {
// NOLINTNEXTLINE(misc-use-anonymous-namespace) — must be at file scope for LD_PRELOAD override.
__attribute__((weak)) void alloc_guard_start() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) void alloc_guard_end() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) long alloc_guard_count() { return 0; }
}

namespace {

// ── Application stubs ─────────────────────────────────────────────────────────

// CountingApp028: records callback invocations per type.
// Used to assert delivery (fromApp/fromAdmin) and admin intercept (toAdmin).
class CountingApp028 : public fixpp::session::Application {
public:
    int from_app_count{0};
    int from_admin_count{0};
    int to_admin_count{0};
    int on_logon_count{0};

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_app_count;
        return {};
    }

    fixpp::core::expected_t<void> fromAdmin(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
        const fixpp::session::SessionId& /*id*/) override {
        ++from_admin_count;
        return {};
    }

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {
        ++to_admin_count;
    }

    void onLogon(const fixpp::session::SessionId& /*id*/) override { ++on_logon_count; }
};

// ── Frame-building helpers ────────────────────────────────────────────────────

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

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq,
                                         std::string_view s, std::string_view t,
                                         int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// ── Outbound capture ──────────────────────────────────────────────────────────

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

// ── Minimal no-op MessageStore double ────────────────────────────────────────

using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

class NullStore final : public MessageStore {
public:
    NullStore() : MessageStore(flush_thunk_for<NullStore>()) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
        direction_t /*dir*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override {
        co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t /*dir*/, bool /*increment*/) noexcept override {
        co_return seqnum_t{1};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }
};

class NullStoreFactory final : public MessageStoreFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/,
        std::pmr::memory_resource* /*mr*/, std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::unique_ptr<MessageStore>(new NullStore());
    }
};

// ── Session fixture ───────────────────────────────────────────────────────────

struct Fixture {
    asio::io_context ioc;
    OutboundCapture capture;
    fixpp::core::EngineConfig eng;
    fixpp::session::SessionConfig cfg;
    std::unique_ptr<fixpp::session::Session> session;

    void feed(const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(
            ioc, session->on_inbound_frame(std::span<const std::byte>(frame)), asio::use_future);
        ioc.run_for(5s);
        ioc.restart();
        (void)fut.get();
    }

    void clear_capture() { capture.frames.clear(); }
};

static std::unique_ptr<Fixture> make_acceptor(
    std::shared_ptr<MessageStoreFactory> store_factory = std::make_shared<NullStoreFactory>(),
    std::uint32_t peer_logon_seq = 1,
    std::shared_ptr<fixpp::session::Application> app = nullptr) {
    auto fix = std::make_unique<Fixture>();

    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::move(store_factory);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    if (app) {
        fix->eng.application = std::move(app);
    }

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    fix->feed(make_logon("FIX.4.4", peer_logon_seq, "CLI", "SRV"));

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "make_acceptor: session must be Active after Logon";

    return fix;
}

static std::unique_ptr<Fixture> make_initiator(
    std::shared_ptr<MessageStoreFactory> store_factory = std::make_shared<NullStoreFactory>(),
    std::shared_ptr<fixpp::session::Application> app = nullptr) {
    auto fix = std::make_unique<Fixture>();

    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};  // disable liveness in LogonSent
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::move(store_factory);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    if (app) {
        fix->eng.application = std::move(app);
    }

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "make_initiator: session must be LogonSent after open()";

    return fix;
}

// ── T004 (US1) — CompID witnesses ────────────────────────────────────────────
//
// RED witnesses written before T005 (session.cpp S1 split).
// Anchors: spec.md FR-003/FR-012, data-model.md I-VCT-1/I-VCT-2/I-VCT-6,
//          contracts C1.1/C1.2/C1.3, SC-001/SC-004/SC-007.
//
// Test naming follows tasks.md T004 list exactly.

// T004 witness 1 — SC-001 relaxed, C1.2
// Steady-state 49/56 mismatch with check_comp_id=false ⇒ frame delivered, no
// disconnect. Post-condition: session stays Active AND fromApp fires once.
// RED before T005: current S1 gate disconnects on ANY CompID mismatch.
TEST(ValidationCompatToggles, CompID_KnobOff_MismatchAccepted) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
    // check_comp_id=false AFTER construction — must set BEFORE the test feed.
    // NB: SessionConfig is frozen at open(); we mutate cfg_ here via the Fixture
    // to simulate the knob being set at config time.  We rebuild the session.
    fix->cfg.check_comp_id = false;
    // Rebuild session with the knob off, re-establish to Active.
    fix->session.reset();
    auto& f = *fix;
    f.session = std::make_unique<fixpp::session::Session>(f.eng, f.cfg);
    auto open_fut = asio::co_spawn(f.ioc, f.session->open(), asio::use_future);
    f.ioc.run_for(1s);
    f.ioc.restart();
    (void)open_fut.get();
    fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Precondition: session must be Active before the mismatching frame";

    // Feed an application message with mismatching 49/56 (reversed CompIDs).
    // Sender = "WRONG_SENDER", Target = "WRONG_TARGET" — neither matches config.
    fix->clear_capture();
    const int from_app_before = app->from_app_count;
    fix->feed(make_fix_frame("FIX.4.4", "D", 2, "WRONG_SENDER", "WRONG_TARGET"));

    // Post-conditions (C1.2, SC-001 relaxed):
    //   1. Session stays Active (no disconnect).
    //   2. Application::fromApp was called — the frame was delivered.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "C1.2/SC-001: with check_comp_id=false, a 49/56 mismatch must NOT disconnect";
    EXPECT_GT(app->from_app_count, from_app_before)
        << "C1.2/SC-001: with check_comp_id=false, mismatching app frame must be delivered "
           "to fromApp";
}

// T004 witness 2 — SC-001 paired, C1.1
// Identical mismatching frame at default (check_comp_id=true) ⇒ disconnect.
// Already GREEN: current behavior.
TEST(ValidationCompatToggles, CompID_Default_MismatchRejected) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
    // cfg.check_comp_id is default true — no change needed.
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Precondition: session must be Active";

    const int from_app_before = app->from_app_count;
    fix->feed(make_fix_frame("FIX.4.4", "D", 2, "WRONG_SENDER", "WRONG_TARGET"));

    // Post-conditions (C1.1, SC-001 default):
    //   1. Session is Disconnected.
    //   2. fromApp was NOT called.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "C1.1/SC-001: at default (check_comp_id=true), a 49/56 mismatch must disconnect";
    EXPECT_EQ(app->from_app_count, from_app_before)
        << "C1.1/SC-001: mismatching frame must NOT be delivered when check_comp_id=true";
}

// T004 witness 3 — US1 AS3
// Matching 49/56 with knob off ⇒ unchanged (frame delivered, session Active).
// Already GREEN: current code lets matching CompIDs through.
TEST(ValidationCompatToggles, CompID_KnobOff_MatchingPathUnchanged) {
    auto app = std::make_shared<CountingApp028>();
    // Rebuild with check_comp_id=false.
    NullStoreFactory sf;
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<NullStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->cfg.check_comp_id = false;  // knob off
    fix->eng.application = app;

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();
    fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Precondition: session Active after Logon";

    // Feed a frame with MATCHING CompIDs.
    const int from_app_before = app->from_app_count;
    fix->feed(make_fix_frame("FIX.4.4", "D", 2, "CLI", "SRV"));

    // Post-conditions (US1 AS3): matching path is unchanged.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "US1 AS3: matching CompIDs with knob off must keep session Active";
    EXPECT_GT(app->from_app_count, from_app_before)
        << "US1 AS3: matching CompID frame must still be delivered";
}

// T004 witness 4 — I-VCT-1, C1.3
// Mismatching BeginString(8) still disconnects with check_comp_id=false.
// Already GREEN: current code combines begin_string + CompID in one gate;
// after T005 begin_string stays strict; before T005 current code also disconnects.
TEST(ValidationCompatToggles, CompID_KnobOff_BeginStringStillStrict) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<NullStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->cfg.check_comp_id = false;  // knob off — CompID check skipped
    fix->eng.application = app;

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();
    fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Precondition: session Active";

    // Feed a frame with WRONG BeginString but CORRECT CompIDs.
    const int from_app_before = app->from_app_count;
    fix->feed(make_fix_frame("FIX.4.2", "D", 2, "CLI", "SRV"));

    // Post-conditions (I-VCT-1, C1.3): BeginString is always enforced.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "I-VCT-1/C1.3: BeginString mismatch must still disconnect even with "
           "check_comp_id=false";
    EXPECT_EQ(app->from_app_count, from_app_before)
        << "I-VCT-1/C1.3: wrong-BeginString frame must NOT be delivered";
}

// T004 witness 5 — I-VCT-2, SC-004, C1.3
// 013 compid_authorization_policy non-allow-listed principal still refused at
// Logon with check_comp_id=false.
// Already GREEN: authz check is in the NotConnected/LogonReceived Logon path,
// not at S1 (data-model D-4 "Untouched"). The knob does not affect it.
//
// Setup: mTLS security profile + allow-list with "ALLOWED_PEER" → "SRV".
// Inject a peer identity CN=NOT_ON_LIST (not in the allow-list).
// Feed a Logon. Expect: session Disconnected (authz refused), NOT Active.
TEST(ValidationCompatToggles, CompID_KnobOff_AuthzAllowListStillEnforced) {
    // We need mTLS + authorization policy + identity injection.
    // Include required headers at call site via the support helpers already included.
    asio::io_context ioc;
    fixpp::core::EngineConfig eng{};
    eng.executor = ioc.get_executor();

    auto app = std::make_shared<CountingApp028>();
    eng.application = app;

    fixpp::session::CompIdAuthorizationPolicy policy;
    policy.add_binding("ALLOWED_PEER", "CLI");  // only ALLOWED_PEER may use CompID "CLI"

    fixpp::session::SessionConfig cfg;
    cfg.role = fixpp::session::session_role::acceptor;
    cfg.sender_comp_id = "SRV";
    cfg.target_comp_id = "CLI";
    cfg.begin_string = "FIX.4.4";
    cfg.security_profile =
        fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
    cfg.compid_authorization_policy = std::move(policy);
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.heartbeat_interval = std::chrono::seconds{30};
    cfg.executor_override = ioc.get_executor();
    cfg.store_factory = std::make_shared<NullStoreFactory>();
    cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    cfg.check_comp_id = false;  // knob off — but authz must still enforce
    std::vector<std::vector<std::byte>> captured;
    cfg.transport_send = [&captured](std::span<const std::byte> data) {
        captured.emplace_back(data.begin(), data.end());
    };

    fixpp::session::Session session{eng, cfg};

    auto open_fut = asio::co_spawn(ioc, session.open(), asio::use_future);
    ioc.run_for(1s);
    ioc.restart();
    (void)open_fut.get();

    // Inject a peer identity that is NOT on the allow-list.
    fixpp::tls::peer_identity bad_pid;
    bad_pid.subject_dn = "CN=NOT_ON_LIST";
    fixpp::test_support::inject_live_identity(session, std::move(bad_pid));

    // Feed a Logon from "CLI" (matching CompID but unauthorized principal).
    auto logon_frame = make_logon("FIX.4.4", 1, "CLI", "SRV");
    auto feed_fut = asio::co_spawn(
        ioc, session.on_inbound_frame(std::span<const std::byte>{logon_frame}), asio::use_future);
    ioc.run_for(2s);
    ioc.restart();
    (void)feed_fut.get();

    // Post-conditions (I-VCT-2, SC-004, C1.3):
    //   Authz allow-list refused the not-on-list principal → Disconnected.
    //   check_comp_id=false MUST NOT bypass this.
    EXPECT_EQ(session.state(), fixpp::session::fsm_state::Disconnected)
        << "I-VCT-2/SC-004/C1.3: 013 authz allow-list must refuse non-listed principal "
           "even with check_comp_id=false";
    EXPECT_EQ(app->on_logon_count, 0)
        << "I-VCT-2/SC-004: onLogon must NOT fire when authz refuses the principal";
}

// T004 witness 6 — I-VCT-6, SC-007, FR-012
// Logon 49 ≠ configured target_comp_id still refused (steady-state-only scope).
// check_comp_id=false must NOT relax the Logon-establishment CompID check.
// Already GREEN: interpret_logon validates CompIDs at NotConnected, which is
// untouched (data-model "Untouched" / FR-012).
TEST(ValidationCompatToggles, CompID_KnobOff_LogonTimeMismatchStillRefused) {
    // Build a session with check_comp_id=false.
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<NullStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->cfg.check_comp_id = false;  // knob off

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::NotConnected)
        << "Precondition: acceptor starts NotConnected before any Logon";

    // Feed a Logon with WRONG SenderCompID (49=WRONG, not our target_comp_id="CLI").
    fix->feed(make_logon("FIX.4.4", 1, "WRONG_PEER", "SRV"));

    // Post-conditions (I-VCT-6, SC-007, FR-012):
    //   Logon-time CompID mismatch → Disconnected regardless of check_comp_id.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "I-VCT-6/SC-007/FR-012: Logon-time CompID mismatch must refuse even with "
           "check_comp_id=false (steady-state-only scope)";
}

}  // namespace
