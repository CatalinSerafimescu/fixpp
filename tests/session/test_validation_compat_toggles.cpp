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
#include "support/pump_until_ready.hpp"

using namespace std::chrono_literals;

// mallocnesia weak-symbol hooks — replaced by LD_PRELOAD; no-ops otherwise.
// Must be at file scope for the LD_PRELOAD override to bind.
#include "support/alloc_guard_markers.hpp"

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

    // A destructor BODY runs before ANY member is destroyed, i.e. at the one
    // moment when ioc/session/cfg/capture are all still alive. That covers every
    // exit path out of a test, including the early `return` an ASSERT_* performs,
    // without reordering anything — and reordering is not an option regardless:
    // `session`'s bound executor is a strand on `ioc` under the default
    // `per_session_strand`, and destroying a strand after its context is an
    // unconditional heap-use-after-free (see the numbered strand rule in
    // `quiesce_on_exit`'s comment in support/pump_until_ready.hpp). `ioc` STAYS
    // FIRST so it is destroyed LAST.
    //
    // It protects fixture MEMBERS only. A caller's temporary passed to feed()
    // dies BEFORE this runs and must be drained in its own scope instead.
    ~Fixture() { fixpp::test_support::drain_or_report(ioc, "Fixture::~Fixture"); }

    void feed(const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(
            ioc, session->on_inbound_frame(std::span<const std::byte>(frame)), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 5s)) {
            // Drain HERE, not in ~Fixture. `frame` binds to storage that is NOT a
            // fixture member — at most call sites a caller's temporary, destroyed
            // at the end of the caller's full-expression, but at some (the
            // NoHeap_RelaxedDeliverPath alloc-guard witness, `too_low_frame`) a
            // named local declared AFTER the fixture, which is destroyed BEFORE
            // it too. Either shape dies long before the fixture. The suspended
            // coroutine holds a span into it, so ~Fixture's drain would RESUME it
            // over dead storage rather than protect it.
            fixpp::test_support::drain_or_report(ioc, "Fixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "Fixture::feed";
            return;
        }
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
    if (!fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s)) {
        fixpp::test_support::drain_or_report(fix->ioc, "make_acceptor");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "make_acceptor";
        // Return the fixture, NOT nullptr: no call site in this file
        // null-checks the result, so nullptr would turn a miss into a
        // null-dereference SEGFAULT, which reports strictly worse than the
        // hang this migration removes (a crashed leg emits no FAILED lines
        // at all).
        //
        // The drain above settles open() BEFORE the fixture escapes.
        // ~Fixture's drain is NOT sufficient for an escaping fixture: it runs
        // only at fixture destruction, and a caller that destroys a fixture
        // MEMBER first — fix->session.reset() in CompID_KnobOff_MismatchAccepted
        // — then pumps ioc resumes this frame over the destroyed Session.
        // Proven: heap-use-after-free, freed by
        // fix->session.reset() in CompID_KnobOff_MismatchAccepted, read in
        // Session::open() (.resume) at src/session/session.cpp:937, resumed
        // from the next run_window_then_ready. Keep returning the fixture
        // rather than nullptr — that part of the reasoning is unchanged.
        //
        // The drain's honest limit: drain_or_report is best-effort — if it
        // reports a residual, the frame is still suspended and the hazard
        // returns; it converts unconditional UB into an observed-and-reported
        // residual, not a guaranteed absence of one.
        return fix;
    }
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
    if (!fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 2s)) {
        fixpp::test_support::drain_or_report(fix->ioc, "make_initiator");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "make_initiator";
        // Return the fixture, NOT nullptr: no call site in this file
        // null-checks the result, so nullptr would turn a miss into a
        // null-dereference SEGFAULT, which reports strictly worse than the
        // hang this migration removes (a crashed leg emits no FAILED lines
        // at all). The drain above settles open() BEFORE the fixture escapes
        // — see make_acceptor's comment above for the proven mechanism
        // (heap-use-after-free, reproduced under ASan) and the drain's
        // honest limits.
        return fix;
    }
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
    ASSERT_TRUE(fixpp::test_support::run_window_then_ready(f.ioc, open_fut, 1s))
        << fixpp::test_support::kWindowMiss << "CompID_KnobOff_MismatchAccepted";
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
    ASSERT_TRUE(fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s))
        << fixpp::test_support::kWindowMiss << "CompID_KnobOff_MatchingPathUnchanged";
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
    ASSERT_TRUE(fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s))
        << fixpp::test_support::kWindowMiss << "CompID_KnobOff_BeginStringStillStrict";
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
    // Not ASSERT_TRUE: this test builds its objects as BLOCK-LOCALS, with no
    // Fixture and therefore no destructor drain. `ioc` is declared FIRST, so it is
    // destroyed LAST — a suspended frame it still owns would be destroyed after
    // every object it references. The drain has to happen here, while they are all
    // alive, and an ASSERT_* would `return` past it.
    if (!fixpp::test_support::run_window_then_ready(ioc, open_fut, 1s)) {
        fixpp::test_support::drain_or_report(ioc, "CompID_KnobOff_AuthzAllowListStillEnforced/open");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "CompID_KnobOff_AuthzAllowListStillEnforced/open";
        return;
    }
    (void)open_fut.get();

    // Inject a peer identity that is NOT on the allow-list.
    fixpp::tls::peer_identity bad_pid;
    bad_pid.subject_dn = "CN=NOT_ON_LIST";
    fixpp::test_support::inject_live_identity(session, std::move(bad_pid));

    // Feed a Logon from "CLI" (matching CompID but unauthorized principal).
    auto logon_frame = make_logon("FIX.4.4", 1, "CLI", "SRV");
    auto feed_fut = asio::co_spawn(
        ioc, session.on_inbound_frame(std::span<const std::byte>{logon_frame}), asio::use_future);
    // As above, and one degree sharper: the coroutine holds a span into
    // `logon_frame`, a block-local declared AFTER `ioc`, so it dies BEFORE the
    // frame does. The drain therefore runs here, inside `logon_frame`'s lifetime.
    //
    // ⚠️ This drain is DEFENSIVE, not load-bearing, and the difference was
    // measured rather than assumed. Deleting it and forcing this site to miss
    // (starve the 2s window under ASan) produces NO fault: with no fixture here,
    // nothing ever RESUMES the frame — `ioc` destroys it still suspended at its
    // initial suspend point, and a `std::span` parameter is trivially
    // destructible, so the destruction is silent. The drain converts "frame
    // destroyed unresumed" into "frame completed", which is better hygiene but
    // is not preventing a demonstrated use-after-free. Contrast Fixture::feed
    // above, where deleting the in-feed drain DOES fault, because ~Fixture's own
    // drain is there to resume the frame over the dead temporary. Do not cite
    // this site as evidence that a scope drain is required wherever a coroutine
    // borrows a block-local; what makes it required is a LATER DRAIN that
    // resumes the frame.
    //
    // This is also the one site in this PR whose Session has a live Transport
    // attached (`inject_live_identity` above installs a NullSinkTransport), and
    // `drain_or_report` deliberately does NOT close a transport — see its warning
    // in support/pump_until_ready.hpp. That gap is unreachable HERE and only here:
    // NullSinkTransport::async_read_some reports EOF immediately and async_write
    // swallows its bytes (tests/support/identity_injecting_transport.hpp:39-70),
    // so no coroutine can park in either, which is the only thing closing the
    // transport would unstick. A transport that can genuinely block needs
    // `quiesce_on_exit` with `.transport` set instead; do not generalise from this
    // site to those.
    if (!fixpp::test_support::run_window_then_ready(ioc, feed_fut, 2s)) {
        fixpp::test_support::drain_or_report(ioc, "CompID_KnobOff_AuthzAllowListStillEnforced/feed");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss
                      << "CompID_KnobOff_AuthzAllowListStillEnforced/feed";
        return;
    }
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
    ASSERT_TRUE(fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s))
        << fixpp::test_support::kWindowMiss << "CompID_KnobOff_LogonTimeMismatchStillRefused";
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
    if (!fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s)) {
        fixpp::test_support::drain_or_report(fix->ioc, "make_acceptor_seqval_off");
        ADD_FAILURE() << fixpp::test_support::kWindowMiss << "make_acceptor_seqval_off";
        // Return the fixture, NOT nullptr: no call site in this file
        // null-checks the result, so nullptr would turn a miss into a
        // null-dereference SEGFAULT, which reports strictly worse than the
        // hang this migration removes (a crashed leg emits no FAILED lines
        // at all). The drain above settles open() BEFORE the fixture escapes
        // — see make_acceptor's comment above for the proven mechanism
        // (heap-use-after-free, reproduced under ASan) and the drain's
        // honest limits.
        return fix;
    }
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
    ASSERT_TRUE(fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s))
        << fixpp::test_support::kWindowMiss << "Seq_KnobOff_LogonTimeTooHighStillRefused";
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

// ── T012 (US3) — Default/combination/no-op witnesses ────────────────────────
//
// Anchors: spec.md FR-007/FR-008/FR-009, SC-003/SC-005; data-model.md
//          I-VCT-7/I-VCT-8/I-VCT-9; contracts C3.1/C3.2/C3.3; [const §VIII.5].
//
// T012 witness list (tasks.md):
//   Default_FieldsAreStrict             — US3 AS2
//   Default_ByteIdenticalBaseline       — SC-003, I-VCT-7, C3.3
//   Combination_Matrix_FourCells        — SC-005, FR-007, I-VCT-8, C3.1
//   Inbound_Only_OutboundUnchanged      — FR-008, I-VCT-9, C3.2
//   NoHeap_RelaxedDeliverPath           — [const §VIII.5], C1 remediation

// T012 witness 1 — US3 AS2
// Freshly default-constructed SessionConfig has check_comp_id==true AND
// validate_sequence_numbers==true (both fields default strict).
// This is a trivial compile+runtime assertion of the contract C1/C2 defaults.
// Already GREEN: T003 set both defaults to true.
TEST(ValidationCompatToggles, Default_FieldsAreStrict) {
    fixpp::session::SessionConfig cfg;

    EXPECT_TRUE(cfg.check_comp_id)
        << "US3 AS2: check_comp_id must default to true (strict) per C1/FR-001";
    EXPECT_TRUE(cfg.validate_sequence_numbers)
        << "US3 AS2: validate_sequence_numbers must default to true (strict) per C2/FR-004";
}

// T012 witness 2 — SC-003, I-VCT-7, C3.3
// With both knobs at default (both=true), a representative scenario is byte-identical
// to today's strict baseline:
//   (a) CompID mismatch at default → session Disconnected (same as CompID_Default_MismatchRejected).
//   (b) Too-low seqnum at default → session Disconnected (same as strict today).
//   (c) Too-high seqnum at default → ResendRequest emitted (same as strict today).
// These are the same as the existing paired witnesses in T004/T006; we assert them
// here to prove byte-identical default behaviour when NEITHER knob is changed from
// the default.
TEST(ValidationCompatToggles, Default_ByteIdenticalBaseline) {
    // Part (a): default session + CompID mismatch → Disconnected (strict baseline).
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
        // Both knobs are default true (not explicitly set).
        ASSERT_TRUE(fix->cfg.check_comp_id)
            << "Precondition: check_comp_id must be default true";
        ASSERT_TRUE(fix->cfg.validate_sequence_numbers)
            << "Precondition: validate_sequence_numbers must be default true";
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Precondition: session must be Active";

        const int from_app_before = app->from_app_count;
        fix->feed(make_fix_frame("FIX.4.4", "D", 2, "WRONG_SENDER", "WRONG_TARGET"));

        // Post-condition (SC-003/I-VCT-7/C3.3): strict baseline → Disconnected.
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "SC-003/I-VCT-7/C3.3: at both-default, CompID mismatch must disconnect "
               "(byte-identical to pre-feature baseline)";
        EXPECT_EQ(app->from_app_count, from_app_before)
            << "SC-003: mismatching frame must not be delivered at default";
    }

    // Part (b): default session + too-low seqnum → Disconnected (strict baseline).
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
        ASSERT_TRUE(fix->cfg.validate_sequence_numbers)
            << "Precondition: validate_sequence_numbers must be default true";
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

        fix->feed(make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV"));  // seq=1, expected=2 → too-low

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "SC-003/I-VCT-7: at default, too-low seqnum must disconnect (strict baseline)";
    }

    // Part (c): default session + too-high seqnum → ResendRequest (strict baseline).
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor(std::make_shared<NullStoreFactory>(), 1, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);
        fix->clear_capture();

        fix->feed(make_fix_frame("FIX.4.4", "D", 5, "CLI", "SRV"));  // seq=5, expected=2 → too-high

        EXPECT_GE(count_frames_with_msgtype(fix->capture.frames, "2"), 1)
            << "SC-003/I-VCT-7: at default, too-high must emit ResendRequest (strict baseline)";
    }
}

// T012 witness 3 — SC-005, FR-007, I-VCT-8, C3.1
// All four knob combinations behave per their axis — the two knobs are independent.
// Each cell is asserted for its DISTINGUISHING outcome (not just end-state).
//
// Cell 1 (both=default): CompID mismatch → Disconnected (C1.1), too-low → Disconnected (C2.1).
// Cell 2 (check_comp_id=false, validate_sequence_numbers=true):
//   CompID mismatch → delivered (C1.2); too-low → Disconnected (C2.1).
// Cell 3 (check_comp_id=true, validate_sequence_numbers=false):
//   CompID mismatch → Disconnected (C1.1); too-low → delivered (C2.2).
// Cell 4 (both=false):
//   CompID mismatch → delivered (C1.2); too-low → delivered (C2.2).
TEST(ValidationCompatToggles, Combination_Matrix_FourCells) {
    // Helper lambda: build an acceptor fixture with specified knob settings.
    auto make_acceptor_with_knobs = [](bool check_cid, bool validate_seq,
                                        std::shared_ptr<fixpp::session::Application> app) {
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
        fix->cfg.reset_seqnum_policy_field =
            fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->cfg.check_comp_id = check_cid;
        fix->cfg.validate_sequence_numbers = validate_seq;
        if (app) fix->eng.application = app;

        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        // Non-fatal: this is a value-returning LAMBDA, and a gtest FATAL assertion
        // is only valid in a void-returning function. Return the fixture, not
        // nullptr — the call sites below dereference the result without a
        // null check, so nullptr would turn a miss into a SEGFAULT, which reports
        // strictly worse than the hang this migration removes.
        if (!fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s)) {
            fixpp::test_support::drain_or_report(
                fix->ioc, "Combination_Matrix_FourCells/make_acceptor_with_knobs");
            // The drain above is DEFENSIVE, not currently load-bearing: none of
            // the call sites below destroys a fixture member before ~Fixture
            // runs (each dereferences the result, calls feed(), and lets the
            // fixture die normally), so it enforces the factory-boundary
            // invariant against future caller changes rather than preventing an
            // active hazard today. The hazard it guards against only appears
            // when a live fixture ESCAPES the miss branch to a caller that can
            // destroy a member before ~Fixture runs — see make_acceptor's
            // comment above for the mechanism (heap-use-after-free, proven under
            // ASan there), not as a reproduction at this site. Keep returning
            // the fixture rather than nullptr — that part of the reasoning is
            // unchanged.
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "Combination_Matrix_FourCells/make_acceptor_with_knobs";
            return fix;
        }
        (void)open_fut.get();
        fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));
        return fix;
    };

    // Cell 1: both-default (check_comp_id=true, validate_sequence_numbers=true)
    // Distinguishing outcome: CompID mismatch → Disconnected, too-low → Disconnected.
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor_with_knobs(true, true, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell1 precondition: Active after Logon";

        // CompID-mismatch test: must disconnect (C1.1).
        fix->feed(make_fix_frame("FIX.4.4", "D", 2, "WRONG", "WRONG"));
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Cell1 (both-default): CompID mismatch must disconnect (C1.1)";
    }
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor_with_knobs(true, true, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

        // Too-low test: must disconnect (C2.1).
        fix->feed(make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV"));  // seq=1, expected=2
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Cell1 (both-default): too-low must disconnect (C2.1)";
    }

    // Cell 2: check_comp_id=false, validate_sequence_numbers=true (seqnum still strict).
    // Distinguishing outcome: CompID mismatch → delivered (C1.2), too-low → Disconnected (C2.1).
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor_with_knobs(false, true, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell2 precondition: Active after Logon";

        // CompID-mismatch test: must be delivered, not disconnect (C1.2).
        const int from_app_before = app->from_app_count;
        fix->feed(make_fix_frame("FIX.4.4", "D", 2, "WRONG", "WRONG"));
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell2 (check_comp_id=off, seqval=on): CompID mismatch must NOT disconnect";
        EXPECT_GT(app->from_app_count, from_app_before)
            << "Cell2: CompID-mismatching frame must be delivered to fromApp";

        // Too-low test: seqnum still strict → Disconnected (C2.1).
        fix->feed(make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV"));  // seq=1, expected=3 now
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Cell2: too-low must still disconnect when validate_sequence_numbers=true";
    }

    // Cell 3: check_comp_id=true, validate_sequence_numbers=false (CompID still strict).
    // Distinguishing outcome: CompID mismatch → Disconnected (C1.1), too-low → delivered (C2.2).
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor_with_knobs(true, false, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell3 precondition: Active after Logon";

        // Too-low test: must be delivered, not disconnect (C2.2).
        const int from_app_before = app->from_app_count;
        fix->feed(make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV"));  // seq=1, expected=2 → too-low
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell3 (check_comp_id=on, seqval=off): too-low must NOT disconnect";
        EXPECT_GT(app->from_app_count, from_app_before)
            << "Cell3: too-low app frame must be delivered to fromApp";

        // CompID-mismatch test: CompID still strict → Disconnected (C1.1).
        fix->feed(make_fix_frame("FIX.4.4", "D", 2, "WRONG", "WRONG"));
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Cell3: CompID mismatch must still disconnect when check_comp_id=true";
    }

    // Cell 4: both-off (check_comp_id=false, validate_sequence_numbers=false).
    // Distinguishing outcome: CompID mismatch → delivered (C1.2), too-low → delivered (C2.2).
    {
        auto app = std::make_shared<CountingApp028>();
        auto fix = make_acceptor_with_knobs(false, false, app);
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell4 precondition: Active after Logon";

        // CompID-mismatch test: must be delivered (C1.2).
        const int from_app_before_compid = app->from_app_count;
        fix->feed(make_fix_frame("FIX.4.4", "D", 2, "WRONG", "WRONG"));
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell4 (both-off): CompID mismatch must NOT disconnect";
        EXPECT_GT(app->from_app_count, from_app_before_compid)
            << "Cell4: CompID-mismatching frame must be delivered";

        // Too-low test: must be delivered (C2.2).
        const int from_app_before_seqlow = app->from_app_count;
        fix->feed(make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV"));  // seq=1, expected=3 now → too-low
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Cell4 (both-off): too-low must NOT disconnect";
        EXPECT_GT(app->from_app_count, from_app_before_seqlow)
            << "Cell4: too-low frame must be delivered to fromApp";
    }
}

// T012 witness 4 — FR-008, I-VCT-9, C3.2
// Neither knob is read on any outbound construction path; outbound frames are
// identical regardless of knob settings.
//
// Strategy: establish an Active session with all four knob combinations and
// compare the outbound Logon frame bytes. All four should produce the same
// outbound Logon (since outbound is unaffected by either knob). We also confirm
// the session can emit after receiving in-order frames and the outbound frame is
// the same across settings.
//
// Anchor: FR-008 (both knobs govern inbound validation only), I-VCT-9.
TEST(ValidationCompatToggles, Inbound_Only_OutboundUnchanged) {
    // Helper: build a fresh acceptor with specified knob settings, then capture
    // the outbound Logon-ack frame (the first outbound frame after open + Logon).
    auto capture_outbound_logon = [](bool check_cid, bool validate_seq) {
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
        fix->cfg.reset_seqnum_policy_field =
            fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->cfg.check_comp_id = check_cid;
        fix->cfg.validate_sequence_numbers = validate_seq;

        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        // Non-fatal for the same reason as the lambda above: a value-returning
        // lambda cannot carry a gtest FATAL assertion. Return whatever was
        // captured; the caller compares the four vectors and fails loudly on its
        // own assertion, which is a better report than an empty vector silently
        // comparing equal to another empty vector would be.
        //
        // No drain needed on this branch, unlike `Combination_Matrix_FourCells`'s
        // lambda above: `fix` here is a lambda-local `unique_ptr<Fixture>`,
        // destroyed at the `return` two lines below — no caller holds it past
        // that point. `~Fixture`'s drain therefore runs microseconds later with
        // every member still alive; it merely completes a frame nothing else
        // would resume, and is harmless here because every referent it touches
        // is a fixture member (cf. the `CompID_KnobOff_AuthzAllowListStillEnforced`
        // block-local drain comment above: what makes a drain required is a
        // LATER DRAIN that resumes the frame). The hazard only appears when a
        // live fixture ESCAPES the miss branch to a caller that can destroy a
        // member before `~Fixture` runs.
        if (!fixpp::test_support::run_window_then_ready(fix->ioc, open_fut, 1s)) {
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "Inbound_Only_OutboundUnchanged/capture_outbound_logon";
            return std::vector<std::vector<std::byte>>(fix->capture.frames);
        }
        (void)open_fut.get();
        fix->feed(make_logon("FIX.4.4", 1, "CLI", "SRV"));
        // Return copies of outbound frames (the Logon-ack and any other admin).
        return std::vector<std::vector<std::byte>>(fix->capture.frames);
    };

    // Capture outbound frames for all four knob combinations.
    auto frames_both_default = capture_outbound_logon(true, true);
    auto frames_compid_off   = capture_outbound_logon(false, true);
    auto frames_seqval_off   = capture_outbound_logon(true, false);
    auto frames_both_off     = capture_outbound_logon(false, false);

    // Post-conditions (FR-008/I-VCT-9/C3.2):
    //   Each combination must produce the same number of outbound frames.
    ASSERT_EQ(frames_both_default.size(), frames_compid_off.size())
        << "I-VCT-9/C3.2: check_comp_id knob must NOT change outbound frame count";
    ASSERT_EQ(frames_both_default.size(), frames_seqval_off.size())
        << "I-VCT-9/C3.2: validate_sequence_numbers knob must NOT change outbound frame count";
    ASSERT_EQ(frames_both_default.size(), frames_both_off.size())
        << "I-VCT-9/C3.2: neither knob must change outbound frame count";

    // Each outbound frame must be byte-identical across all four combinations.
    for (std::size_t i = 0; i < frames_both_default.size(); ++i) {
        EXPECT_EQ(frames_both_default[i], frames_compid_off[i])
            << "I-VCT-9/C3.2: check_comp_id=off must NOT change outbound frame[" << i << "]";
        EXPECT_EQ(frames_both_default[i], frames_seqval_off[i])
            << "I-VCT-9/C3.2: validate_sequence_numbers=off must NOT change outbound frame["
            << i << "]";
        EXPECT_EQ(frames_both_default[i], frames_both_off[i])
            << "I-VCT-9/C3.2: both-off must NOT change outbound frame[" << i << "]";
    }

    // Also assert that the SeqnumManager's outbound counter (next_outbound) is the
    // same across all combinations (confirming our OWN outgoing seqnums are unchanged).
    // We do this by confirming all four sessions produced the same number of outbound
    // frames (already asserted above) AND the outbound Logon carries the same seqnum.
    ASSERT_FALSE(frames_both_default.empty())
        << "I-VCT-9: at least one outbound frame must be emitted (the Logon-ack)";

    // Verify the outbound Logon seqnum (tag 34) is identical across all four.
    const std::string seqnum_default = extract_tag(frames_both_default[0], 34);
    EXPECT_EQ(seqnum_default, extract_tag(frames_compid_off[0], 34))
        << "I-VCT-9/C3.2: check_comp_id=off must NOT change outbound seqnum (tag 34)";
    EXPECT_EQ(seqnum_default, extract_tag(frames_seqval_off[0], 34))
        << "I-VCT-9/C3.2: validate_sequence_numbers=off must NOT change outbound seqnum";
    EXPECT_EQ(seqnum_default, extract_tag(frames_both_off[0], 34))
        << "I-VCT-9/C3.2: both-off must NOT change outbound seqnum (tag 34)";

    // Verify our own CompIDs (49/56) in outbound frames are unchanged.
    const std::string sender_default = extract_tag(frames_both_default[0], 49);
    const std::string target_default = extract_tag(frames_both_default[0], 56);
    EXPECT_EQ(sender_default, extract_tag(frames_compid_off[0], 49))
        << "I-VCT-9/C3.2: check_comp_id=off must NOT change our own 49 (SenderCompID)";
    EXPECT_EQ(target_default, extract_tag(frames_compid_off[0], 56))
        << "I-VCT-9/C3.2: check_comp_id=off must NOT change our own 56 (TargetCompID)";
    EXPECT_EQ("SRV", sender_default) << "I-VCT-9: our outbound 49 must be SRV";
    EXPECT_EQ("CLI", target_default) << "I-VCT-9: our outbound 56 must be CLI";
}

// T012 witness 5 — [const §VIII.5], plan IX.1, C1 remediation
// The S4 deliver-without-advance path (validate_sequence_numbers=false + out-of-order
// frame) performs no new global-heap allocation.
//
// BINDING gate: mallocnesia LD_PRELOAD interceptor wrapping the session binary.
// The alloc_guard_count() weak symbol is replaced by mallocnesia with a live
// counter; the _mallocnesia ctest companion in CMakeLists.txt runs this binary
// under LD_PRELOAD via tools/check_alloc.py.
//
// Without LD_PRELOAD: the weak stubs are no-ops and alloc_guard_count() returns 0
// (the test still asserts the functional post-condition; the no-heap claim is only
// proved by the _mallocnesia ctest variant).
//
// [[feedback_tracking_pmr_resource_false_pass]]: a counting_resource alone is a
// false-pass because non-PMR std::vector / global-new escapes via global malloc,
// invisible to the PMR. The LD_PRELOAD is the binding proof.
//
// Warm-up: the asio per-thread recycler (cancellation_slot / promise frame) is
// primed by a warm-up pass of the SAME path before the guard window is opened,
// so the measured window is in steady state.
TEST(ValidationCompatToggles, NoHeap_RelaxedDeliverPath) {
    // ── Setup OUTSIDE the guarded window ──────────────────────────────────────
    // Build a session with validate_sequence_numbers=false and drive to Active.
    // All allocation (Session ctor, coroutine frames, asio internals) happens here.
    auto app = std::make_shared<CountingApp028>();
    auto fix = make_acceptor_seqval_off(app);
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Precondition: session must be Active before the guarded window";

    // Pre-build the inbound frame OUTSIDE the guard window so the vector allocation
    // for the frame bytes does not appear inside the window.
    // We use seq=1 (too-low, expected=2) — a representative S4 too-low path.
    const auto too_low_frame = make_fix_frame("FIX.4.4", "D", 1, "CLI", "SRV");

    // Warm-up: run the S4 deliver-without-advance path several times to prime the
    // asio per-thread recycler (cancellation_slot's thread_info_base + promise frame
    // recycling allocates on the FIRST call on a thread; afterwards it is zero-heap).
    // After warm-up, fromApp has been called N times; we snapshot the count.
    constexpr int kWarmup = 8;
    for (int i = 0; i < kWarmup; ++i) {
        fix->feed(too_low_frame);
        // Confirm the warm-up passes deliver the frame (session stays Active).
        if (fix->session->state() != fixpp::session::fsm_state::Active) {
            GTEST_FAIL() << "Warm-up: session must stay Active after too-low with "
                         << "validate_sequence_numbers=false (iteration " << i << ")";
        }
    }

    const int from_app_snapshot = app->from_app_count;

    // ── Guarded window: one S4 deliver-without-advance invocation ────────────
    // NB: feed()'s miss branch now allocates inside this window (drain_or_report's
    // run_for and ADD_FAILURE), so a miss here would make alloc_guard_end() exit(1)
    // rather than report under LD_PRELOAD=libmallocnesia.so. Currently unreachable:
    // this file's mallocnesia companion is if(FALSE)-disabled (REMAINING-WORK item
    // 13), and this is not a regression — a miss here hung on main.
    if (alloc_guard_start) alloc_guard_start();

    fix->feed(too_low_frame);

    const long heap_allocs = alloc_guard_count ? alloc_guard_count() : 0L;
    if (alloc_guard_end) alloc_guard_end();
    // ── End of guarded window ─────────────────────────────────────────────────

    // Functional post-condition: frame was delivered to fromApp.
    EXPECT_GT(app->from_app_count, from_app_snapshot)
        << "[const §VIII.5]: S4 deliver-without-advance path must still deliver the "
           "frame to fromApp inside the guarded window";
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "[const §VIII.5]: session must stay Active after S4 deliver-without-advance";

    // No-heap post-condition (binding gate = mallocnesia LD_PRELOAD, not just this
    // assertion — see the _mallocnesia ctest companion in CMakeLists.txt).
    EXPECT_EQ(heap_allocs, 0L)
        << "[const §VIII.5]: S4 deliver-without-advance must not touch the global heap; "
           "heap_allocs=" << heap_allocs
        << "; run under LD_PRELOAD=tools/mallocnesia/libmallocnesia.so to verify. "
        << "[[feedback_tracking_pmr_resource_false_pass]]";
}

}  // namespace
