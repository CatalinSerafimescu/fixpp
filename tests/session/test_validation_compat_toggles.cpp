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

// ── Frame-scanning helper ────────────────────────────────────────────────────

// extract_tag: find the value of tag N in a raw FIX frame.
// Returns "" if absent. (Mirrors test_next_expected_msgseqnum.cpp:205.)
static std::string extract_tag(const std::vector<std::byte>& frame, int tag) {
    const auto* data = reinterpret_cast<const char*>(frame.data());
    std::string sv(data, frame.size());
    const std::string needle = std::to_string(tag) + "=";
    auto pos = sv.find(needle);
    if (pos == std::string::npos) return "";
    pos += needle.size();
    auto end = sv.find('\x01', pos);
    if (end == std::string::npos) return sv.substr(pos);
    return sv.substr(pos, end - pos);
}

// count_frames_with_msgtype: returns the number of captured frames whose 35= equals mt.
static int count_frames_with_msgtype(const std::vector<std::vector<std::byte>>& frames,
                                     std::string_view mt) {
    int n = 0;
    for (const auto& f : frames) {
        if (extract_tag(f, 35) == mt) ++n;
    }
    return n;
}

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

// ── Fixture builder with validate_sequence_numbers=false ─────────────────────
//
// Builds an acceptor session with the seqnum-validation knob OFF so the tests
// below can be written without repeating the full construction boilerplate.
// After construction the session is Active and next_inbound == 2 (the peer's
// Logon at seq=1 was accepted and advanced the counter).

static std::unique_ptr<Fixture> make_acceptor_seqval_off(
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
    fix->cfg.store_factory = std::make_shared<NullStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.validate_sequence_numbers = false;  // knob off
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    if (app) {
        fix->eng.application = std::move(app);
    }

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "make_acceptor_seqval_off: session must be Active after Logon";

    return fix;
}

// ── T006 (US2) — Ordinary-seqnum witnesses ───────────────────────────────────
//
// RED witnesses written before T008/T009 (session.cpp S2/S4 edits).
// Anchors: spec.md FR-006, SC-002/SC-006/SC-007; data-model.md I-VCT-3/4/5/6/10;
//          contracts C2.2/C2.3/C2.4/C2.5; tasks.md T006.
//
// After make_acceptor_seqval_off: session is Active, next_inbound == 2.
//   too-high example: feed seq=5  (expected=2, forward gap)
//   too-low  example: feed seq=1  (expected=2, replay)
//   exact    example: feed seq=2  (expected=2, in-order)

// T006 witness 1 — SC-002, C2.2, I-VCT-3
// Forward gap with validate_sequence_numbers=false ⇒ frame delivered (fromApp),
// ZERO ResendRequest(35=2) on wire, session NOT in AwaitingResend.
// RED before T008: current S2 guard always emits ResendRequest and returns.
TEST(ValidationCompatToggles, Seq_KnobOff_TooHigh_NoResendRequest) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Precondition: session Active, next_inbound=2";

    fix->clear_capture();
    const int from_app_before = app->from_app_count;

    // Feed an app frame at seq=5 (expected=2 → too-high gap).
    fix->feed(make_fix_frame("FIX.4.4", "D", 5, "CLI", "SRV"));

    // Post-conditions (SC-002, C2.2, I-VCT-3):
    //   1. Session stays Active.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "SC-002/C2.2/I-VCT-3: too-high with knob off must keep session Active";
    //   2. Zero ResendRequest frames on wire.
    EXPECT_EQ(count_frames_with_msgtype(fix->capture.frames, "2"), 0)
        << "SC-002/C2.2/I-VCT-3: ZERO ResendRequest(35=2) must be emitted with knob off";
    //   3. Frame delivered to fromApp (it is an app msgtype 35=D).
    EXPECT_GT(app->from_app_count, from_app_before)
        << "SC-002/C2.2: too-high app frame must be delivered to fromApp with knob off";
}

// T006 witness 2 — SC-002, C2.2
// Too-low (replay) with validate_sequence_numbers=false ⇒ delivered (fromApp),
// session stays Active (no fatal disconnect).
// RED before T009: current S4 Arm B disconnects on too-low non-PossDup non-HB.
TEST(ValidationCompatToggles, Seq_KnobOff_TooLow_NoDisconnect_Delivered) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Precondition: session Active, next_inbound=2";

    const int from_app_before = app->from_app_count;

    // Feed an app frame at seq=1 (expected=2 → too-low replay).
    fix->feed(make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV"));

    // Post-conditions (SC-002, C2.2):
    //   1. Session stays Active (not Disconnected).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "SC-002/C2.2: too-low with knob off must NOT disconnect";
    //   2. Frame delivered to fromApp.
    EXPECT_GT(app->from_app_count, from_app_before)
        << "SC-002/C2.2: too-low app frame must be delivered to fromApp with knob off";
}

// T006 witness 3 — SC-002 paired, C2.1
// Default (validate_sequence_numbers=true): too-high → ResendRequest + AwaitingResend;
// too-low → disconnect. Verifies existing behaviour is unchanged at default.
// Already GREEN: this is the current code path.
TEST(ValidationCompatToggles, Seq_Default_OutOfOrder_RecoveryAndDisconnect) {
    // Test part A: too-high at default → ResendRequest emitted.
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
        // cfg.validate_sequence_numbers is default true.
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

        fix->clear_capture();
        // Feed too-high (seq=5, expected=2).
        fix->feed(make_fix_frame("FIX.4.4", "D", 5, "CLI", "SRV"));

        EXPECT_GE(count_frames_with_msgtype(fix->capture.frames, "2"), 1)
            << "C2.1/SC-002 default: too-high must emit at least one ResendRequest(35=2)";
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "C2.1: session stays Active (enters AwaitingResend, not Disconnected)";
    }

    // Test part B: too-low at default → Disconnected.
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

        // Feed too-low (seq=1, expected=2).
        fix->feed(make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV"));

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "C2.1/SC-002 default: too-low must disconnect (fatal)";
    }
}

// T006 witness 4 — SC-006, I-VCT-4, C2.3
// Out-of-order frame does NOT advance the counter; a following exact-expected
// frame DOES advance it.
// Asserts counter value directly, not just session state.
// RED before T008+T009: the too-high arm currently returns before S4 can deliver;
// counter may or may not move after disconnect.
TEST(ValidationCompatToggles, Seq_KnobOff_ExactMatchOnlyAdvance) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    // Pre-condition: counter is at 2 after the Logon exchange.
    const auto& smgr = fix->session->seqnum_mgr_test_access();
    ASSERT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "Precondition: next_inbound must be 2 before test";

    // Feed an out-of-order frame (seq=5, too-high gap). Counter must NOT advance.
    fix->feed(make_fix_frame("FIX.4.4", "D", 5, "CLI", "SRV"));
    EXPECT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "SC-006/I-VCT-4/C2.3: counter must NOT advance on out-of-order (too-high) frame";
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "SC-006: session must stay Active after too-high with knob off";

    // Feed an exact-match frame (seq=2). Counter MUST advance to 3.
    fix->feed(make_fix_frame("FIX.4.4", "D", 2, "CLI", "SRV"));
    EXPECT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{3})
        << "SC-006/I-VCT-4/C2.3: counter MUST advance to 3 on exact-match frame (seq=2)";
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "SC-006: session must stay Active after exact-match frame";
}

// T006 witness 5 — C2.2, S4
// Out-of-order admin msgtype → fromAdmin; app msgtype → fromApp.
// Both must be dispatched; neither must disconnect.
// RED before T009: current S4 disconnects before dispatch.
TEST(ValidationCompatToggles, Seq_KnobOff_AdminVsAppFanOut) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    // Snapshot counters.
    const int from_admin_before = app->from_admin_count;
    const int from_app_before = app->from_app_count;

    // Feed a too-low admin frame: Heartbeat(35=0) at seq=1.
    // NOTE: the C2.2 carve-out says too-low Heartbeat(35=0) is SILENTLY DROPPED
    // (pre-existing too-low Heartbeat silent-drop at line 2234).
    // So we use a different admin type. 35=3 (Reject) is admin.
    // Use seq=1 (too-low) for the admin frame.
    fix->feed(make_fix_frame("FIX.4.4", "3", 1, "CLI", "SRV"));
    EXPECT_GT(app->from_admin_count, from_admin_before)
        << "C2.2/S4: too-low admin (35=3) must dispatch to fromAdmin with knob off";
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "C2.2: session must stay Active after too-low admin with knob off";

    // Feed a too-high app frame: OrderSingle(35=D) at seq=5.
    const int from_app_before2 = app->from_app_count;
    fix->feed(make_fix_frame("FIX.4.4", "D", 5, "CLI", "SRV"));
    EXPECT_GT(app->from_app_count, from_app_before2)
        << "C2.2/S4: too-high app (35=D) must dispatch to fromApp with knob off";
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "C2.2: session must stay Active after too-high app with knob off";
}

// T006 witness 6 — I-VCT-5, C2.4, FR-006
// PossDup(43=Y)-flagged frame still flows through the 021 Stage-1/Stage-2
// disposition path even with validate_sequence_numbers=false.
// With redeliver_poss_dup=false (default): a too-low 43=Y app frame is silently
// dropped (row 7 stay Active), NOT delivered to fromApp (that is the Stage-2 row-7
// disposition). For an in-sequence 43=Y app frame: delivered (redeliver check
// happens after check_inbound, but row-7 applies only on check_inbound failure).
//
// The key invariant: with knob off, PossDup frames still go through the 021 path
// and are NOT rerouted through the S4 deliver-without-advance path.
// We verify that a too-low 43=Y admin frame is silently dropped (row 6 admin-ignore)
// just like with the knob on, proving Stage-2 is still entered.
// RED before T008+T009: currently the Stage-2 check runs on check_inbound failure;
// after T009 we must preserve this (S4 new arm must check poss_dup_flag first,
// but actually per the code structure: Stage-2 runs at line 2252 (poss_dup_flag=="Y")
// BEFORE Arm B at line 2282 — so PossDup handling is already upstream of the S4 site).
// Therefore this test should already be GREEN after T009 only IF the knob-off arm is
// placed AFTER the Stage-2 PossDup block (i.e. as a guard inside Arm B only).
//
// The witness: too-low PossDup(43=Y) admin frame → session Active (row 6 drop),
// fromAdmin NOT fired (silently dropped per 021 row 6).
TEST(ValidationCompatToggles, Seq_KnobOff_PossDupRetained) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    const int from_admin_before = app->from_admin_count;

    // Build a too-low admin frame with PossDupFlag(43)=Y and OrigSendingTime(122).
    // 35=3 (Reject) is admin, seq=1 (too-low, expected=2).
    const std::string extra = field(43, "Y") + field(122, "20240101-00:00:00.000");
    fix->feed(make_fix_frame("FIX.4.4", "3", 1, "CLI", "SRV", extra));

    // Post-conditions (I-VCT-5, C2.4):
    //   1. Session stays Active (not disconnected).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "I-VCT-5/C2.4: too-low PossDup admin frame must keep session Active";
    //   2. fromAdmin NOT fired (021 row 6: admin PossDup is silently dropped, even with knob off).
    EXPECT_EQ(app->from_admin_count, from_admin_before)
        << "I-VCT-5/C2.4: admin PossDup too-low must be silently dropped (row 6), "
           "not delivered via S4 with knob off";
}

// T006 witness 7 — I-VCT-10, C2.5
// Unparseable MsgSeqNum (seq==0) is still fatal even with validate_sequence_numbers=false.
// The seq==0 check at line 2026 runs BEFORE the S2 too-high guard; the knob does NOT
// apply to it.
// Already GREEN: the seq==0 check is strictly before S2 and is not gated on any knob.
// We confirm it as a carve-out assertion — it MUST remain green after T008/T009.
TEST(ValidationCompatToggles, Seq_KnobOff_SeqZeroStillFatal) {
    auto fix = make_acceptor_seqval_off();
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    // Build a frame with 34=0 (MsgSeqNum=0 → parse_seqnum returns 0 → fatal).
    // We construct a frame directly with seq=0.
    fix->feed(make_fix_frame("FIX.4.4", "D", 0, "CLI", "SRV"));

    // Post-condition (I-VCT-10, C2.5):
    // seq==0 is session-fatal regardless of validate_sequence_numbers.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "I-VCT-10/C2.5: seq==0 must STILL be fatal even with validate_sequence_numbers=false";
}

// T006 witness 8 — N3 carve-out, C2.2
// Too-low Heartbeat(35=0) is silently dropped (not delivered) even with
// validate_sequence_numbers=false. The pre-existing silent-drop at line 2237-2246
// (hdr.msg_type == "0" check inside the check_inbound failure branch) runs before
// Arm B, so even when we add knob-off deliver-to-app, we must check that the
// Heartbeat silent-drop is PRESERVED.
// Already GREEN for the Heartbeat path: the silent-drop is upstream of S4 Arm B.
// This test CONFIRMS the carve-out is retained after T009.
TEST(ValidationCompatToggles, Seq_KnobOff_TooLowHeartbeatStillDropped) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    const int from_admin_before = app->from_admin_count;

    // Feed a too-low Heartbeat: 35=0, seq=1 (expected=2).
    fix->feed(make_fix_frame("FIX.4.4", "0", 1, "CLI", "SRV"));

    // Post-conditions (N3 carve-out, C2.2):
    //   1. Session stays Active (silently dropped, not fatal, not delivered).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "N3/C2.2: too-low Heartbeat must be silently dropped (Active, not Disconnected)";
    //   2. fromAdmin NOT fired (silently dropped).
    EXPECT_EQ(app->from_admin_count, from_admin_before)
        << "N3/C2.2: too-low Heartbeat must NOT be delivered to fromAdmin with knob off";
}

// T006 witness 9 — I-VCT-6, SC-007, FR-012
// Logon-time too-high still takes the strict path (disconnects).
// validate_sequence_numbers relaxes only the Active/steady-state path.
// The Logon/establishment path (NotConnected handler) is entirely untouched.
// Already GREEN: the NotConnected handler is not changed.
// Confirm as a carve-out assertion — must remain green after T008/T009.
TEST(ValidationCompatToggles, Seq_KnobOff_LogonTimeTooHighStillRefused) {
    // Build an acceptor from scratch with validate_sequence_numbers=false.
    // Do NOT use make_acceptor_seqval_off (that already feeds a Logon).
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
    fix->cfg.validate_sequence_numbers = false;  // knob off
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::NotConnected)
        << "Precondition: acceptor starts NotConnected";

    // Feed a Logon with seq=5 (too-high; first expected is 1).
    fix->feed(make_logon("FIX.4.4", 5, "CLI", "SRV"));

    // Post-condition (I-VCT-6, SC-007, FR-012):
    // Logon-time too-high still disconnects (knob does NOT relax Logon establishment).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
        << "I-VCT-6/SC-007/FR-012: Logon too-high must STILL disconnect with "
           "validate_sequence_numbers=false (steady-state-only scope)";
}

// ── T007 (US2) — SequenceReset(35=4) witnesses ──────────────────────────────
//
// RED witnesses written before T010/T011 (session.cpp S6/S7 edits).
// Anchors: spec.md FR-013, SC-008; data-model.md I-VCT-11; contracts C2.7.
//
// After make_acceptor_seqval_off: session is Active, next_inbound == 2.
//
// Helper: build a SequenceReset(35=4) frame.
//   reset_mode (GapFillFlag not set):  make_seq_reset_frame(bs, seq, new_seqno, "SND", "TGT")
//   gapfill_mode (123=Y):              make_seq_reset_frame(bs, seq, new_seqno, "SND", "TGT", true)

static std::vector<std::byte> make_seq_reset_frame(std::string_view bs, std::uint32_t seq,
                                                    std::uint32_t new_seqno, std::string_view sender,
                                                    std::string_view target,
                                                    bool gap_fill = false) {
    std::string extra;
    extra += field(36, std::to_string(new_seqno));
    if (gap_fill) {
        extra += field(123, "Y");
    }
    return make_fix_frame(bs, "4", seq, sender, target, extra);
}

// T007 witness 1 — I-VCT-11, SC-008, C2.7
// Reset-mode SequenceReset(35=4, GapFillFlag≠Y), S6 path.
// With validate_sequence_numbers=false: apply_inbound_sequence_reset NOT called,
// delivered to fromAdmin, counter UNCHANGED.
// RED before T010: current S6 always calls apply_inbound_sequence_reset.
TEST(ValidationCompatToggles, SeqReset_KnobOff_ResetMode_NewSeqNoNotApplied) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    const auto& smgr = fix->session->seqnum_mgr_test_access();
    // Pre-condition: counter is 2 after Logon.
    ASSERT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "Precondition: next_inbound must be 2 before test";

    const int from_admin_before = app->from_admin_count;

    // Feed a reset-mode SequenceReset(35=4, no 123=Y) with NewSeqNo=99 at seq=5 (out-of-order).
    // With knob off, NewSeqNo=99 must NOT be applied.
    fix->feed(make_seq_reset_frame("FIX.4.4", 5, 99, "CLI", "SRV", false));

    // Post-conditions (I-VCT-11, SC-008, C2.7):
    //   1. Counter did NOT jump to 99.
    EXPECT_NE(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{99})
        << "I-VCT-11/SC-008/C2.7: NewSeqNo(99) must NOT be applied with validate_sequence_numbers=false";
    //   2. Counter is UNCHANGED (still 2, not incremented either).
    EXPECT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "I-VCT-11/SC-008/C2.7: counter must remain UNCHANGED (deliver-without-advance) for reset-mode 35=4";
    //   3. Delivered to fromAdmin.
    EXPECT_GT(app->from_admin_count, from_admin_before)
        << "I-VCT-11/SC-008/C2.7: reset-mode 35=4 must be delivered to fromAdmin with knob off";
    //   4. Session stays Active (reset-mode intercept is before the seqnum gate; no disconnect).
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "I-VCT-11: session must remain Active after knob-off reset-mode 35=4";
}

// T007 witness 2 — I-VCT-11, SC-008, C2.7
// Gapfill-mode SequenceReset(35=4, 123=Y), out-of-order (too-high), S7 path.
// With validate_sequence_numbers=false: S2 is guarded off so too-high gapfill
// falls to S4 deliver-without-advance. NewSeqNo NOT applied, counter UNCHANGED.
// RED before T010/T011: with knob off, too-high 35=4 gapfill currently hits S2
// which emits ResendRequest and returns (never reaches S7).
TEST(ValidationCompatToggles, SeqReset_KnobOff_GapfillOutOfOrder_NewSeqNoNotApplied) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    const auto& smgr = fix->session->seqnum_mgr_test_access();
    ASSERT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "Precondition: next_inbound must be 2 before test";

    const int from_admin_before = app->from_admin_count;

    // Feed an out-of-order (too-high, seq=5) gapfill SequenceReset(35=4, 123=Y) with NewSeqNo=99.
    // With knob off: S2 guarded → falls to S4 deliver-without-advance.
    // NewSeqNo=99 must NOT be applied; counter unchanged; delivered to fromAdmin.
    fix->feed(make_seq_reset_frame("FIX.4.4", 5, 99, "CLI", "SRV", true));

    // Post-conditions (I-VCT-11, SC-008, C2.7):
    //   1. Counter did NOT jump to 99.
    EXPECT_NE(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{99})
        << "I-VCT-11/SC-008/C2.7: NewSeqNo(99) must NOT be applied for out-of-order gapfill 35=4";
    //   2. Counter UNCHANGED (too-high → deliver-without-advance via S4).
    EXPECT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "I-VCT-11/SC-008/C2.7: counter must remain UNCHANGED for out-of-order gapfill 35=4 knob off";
    //   3. Delivered to fromAdmin (35=4 is admin).
    EXPECT_GT(app->from_admin_count, from_admin_before)
        << "I-VCT-11/SC-008/C2.7: out-of-order gapfill 35=4 must be delivered to fromAdmin";
    //   4. Session stays Active.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "I-VCT-11: session must remain Active after knob-off out-of-order gapfill 35=4";
}

// T007 witness 3 — SC-008 special case, I-VCT-11
// Exact-match gapfill SequenceReset(35=4, 123=Y), S7 path.
// With validate_sequence_numbers=false: exact-match passes check_inbound → counter
// advances by 1 via ordinary exact-match path (S5), THEN S7 is bypassed so NewSeqNo
// is still NOT applied. The +1 is a fixpp ordering artifact, NOT QFJ-parity.
// RED before T011: current S7 always calls apply_inbound_sequence_reset (which jumps
// counter to NewSeqNo). After T011 the counter must be old+1, NOT NewSeqNo.
TEST(ValidationCompatToggles, SeqReset_KnobOff_GapfillExactMatch_AdvancesByOne_NewSeqNoNotApplied) {
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    const auto& smgr = fix->session->seqnum_mgr_test_access();
    // Pre-condition: counter is 2.
    ASSERT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
        << "Precondition: next_inbound must be 2 before test";

    const int from_admin_before = app->from_admin_count;

    // Feed an exact-match (seq=2 == expected=2) gapfill SequenceReset(35=4, 123=Y)
    // with NewSeqNo=99. With knob off:
    //   - S5 (check_inbound success) advances counter from 2 to 3.
    //   - S7 bypassed → NewSeqNo=99 NOT applied.
    fix->feed(make_seq_reset_frame("FIX.4.4", 2, 99, "CLI", "SRV", true));

    // Post-conditions (SC-008 special case, I-VCT-11):
    //   1. Counter advanced by exactly 1 (from 2 to 3) via ordinary exact-match path.
    EXPECT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{3})
        << "SC-008/I-VCT-11: exact-match gapfill 35=4 must advance counter by +1 via "
           "ordinary exact-match path (fixpp ordering artifact)";
    //   2. Counter did NOT jump to NewSeqNo=99.
    EXPECT_NE(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{99})
        << "SC-008/I-VCT-11: NewSeqNo(99) must NOT be applied even for exact-match gapfill 35=4";
    //   3. Delivered to fromAdmin (35=4 is admin).
    EXPECT_GT(app->from_admin_count, from_admin_before)
        << "SC-008/I-VCT-11: exact-match gapfill 35=4 must be delivered to fromAdmin with knob off";
    //   4. Session stays Active.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "SC-008/I-VCT-11: session must remain Active after knob-off exact-match gapfill 35=4";
}

// T007 witness 4 — SC-008 paired, C2.7
// Default (validate_sequence_numbers=true): both reset-mode and gapfill-mode
// SequenceReset(35=4) apply NewSeqNo exactly as today.
// Already GREEN: current S6/S7 always call apply_inbound_sequence_reset at default.
TEST(ValidationCompatToggles, SeqReset_Default_NewSeqNoApplied) {
    // Part A: reset-mode (GapFillFlag≠Y) at default → NewSeqNo IS applied.
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

        const auto& smgr = fix->session->seqnum_mgr_test_access();
        ASSERT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
            << "Precondition: next_inbound must be 2";

        // Feed a reset-mode 35=4 with NewSeqNo=10 at seq=5 (any seq — reset-mode bypasses gate).
        fix->feed(make_seq_reset_frame("FIX.4.4", 5, 10, "CLI", "SRV", false));

        // Post-conditions (SC-008 paired, C2.7 default=true):
        EXPECT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{10})
            << "SC-008/C2.7 default: reset-mode 35=4 MUST apply NewSeqNo(10) at default (strict)";
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "SC-008/C2.7: session must remain Active after reset-mode 35=4 at default";
    }

    // Part B: gapfill-mode (123=Y) at default, exact-match → NewSeqNo IS applied.
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

        const auto& smgr = fix->session->seqnum_mgr_test_access();
        ASSERT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{2})
            << "Precondition: next_inbound must be 2";

        // Feed an exact-match (seq=2) gapfill 35=4 with NewSeqNo=10 at default.
        fix->feed(make_seq_reset_frame("FIX.4.4", 2, 10, "CLI", "SRV", true));

        // Post-conditions (SC-008 paired, C2.7 default=true):
        EXPECT_EQ(smgr.next_inbound_unsafe(), fixpp::session::seqnum_t{10})
            << "SC-008/C2.7 default: gapfill-mode 35=4 MUST apply NewSeqNo(10) at default (strict)";
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "SC-008/C2.7: session must remain Active after exact-match gapfill 35=4 at default";
    }
}

}  // namespace
