// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_persistent_seqnum_hydrate.cpp
//
// 029-persistent-seqnum-hydrate unit test suite.
//
// Phase 1 (Setup) — T001: skeleton compiles and registers.
// Real witnesses (T006/T008/T009/T012) are added in later phases.
//
// Fixture shape mirrors test_next_expected_msgseqnum.cpp:
//   struct OutboundCapture — captures outbound frames via transport_send_.
//   class CountingApp029 — Application subclass recording callback invocations.
//   struct Fixture — io_context + SessionConfig + Session under test.
//   make_acceptor / make_initiator — helpers that build Active / LogonSent sessions.
//
// Tests are free TEST(...) macros (not TEST_F) per tasks.md T001 convention.
//
// FaultStore: fault-injecting / callback-observing test MessageStore for W3/W6/W14.
//   - Pre-seeded inbound/outbound counters for next_seqnum(dir, false).
//   - Configurable next_seqnum failure on the Nth call (for read/write-failure injection).
//   - Observable durable counter value at callback time.
//
// Anchors: spec.md FR-001..012, SC-001..006; data-model.md W1..W14;
//          contracts/seqnum-hydrate.md C1..C4.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

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

// ── Application stub ──────────────────────────────────────────────────────────

// CountingApp029: records callback invocations per type.
// Used to assert delivery (fromApp/fromAdmin) and lifecycle notifications.
class CountingApp029 : public fixpp::session::Application {
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

// ── Frame-building helpers (mirror test_next_expected_msgseqnum.cpp) ──────────

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
                                         std::string_view s, std::string_view t, int hbt = 30) {
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

// ── Frame field extractor ─────────────────────────────────────────────────────

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

// ── FaultStore: fault-injecting / callback-observing test MessageStore ─────────
//
// Supports:
//   (a) Pre-seeded inbound/outbound counters returned by next_seqnum(dir, false).
//   (b) Configurable failure on the Nth next_seqnum call (for W3/W6/W14 injection).
//   (c) Observable durable_inbound: the counter value at the time of the last
//       next_seqnum(inbound, true) call — lets tests verify persist ordering.
//
// IMPORTANT: next_seqnum(dir, true) simulates a persistent-store write:
//   - increments the internal counter and records the new value in durable_inbound.
//   - Returns an error if the call count reaches fail_on_nth_call_.
//
// next_seqnum(dir, false) is the read path used by ensure_hydrated_:
//   - Returns the pre-seeded counter for that direction.
//   - Also subject to fail_on_nth_call_ (for W14 read-failure injection).

using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

class FaultStore final : public MessageStore {
public:
    // seeded_inbound / seeded_outbound: values returned by next_seqnum(dir, false).
    // fail_on_nth_call: if nonzero, the Nth call to next_seqnum fails with
    //   store_io_failure. Calls are counted across both directions and both
    //   increment/read variants.
    explicit FaultStore(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                        int fail_on_nth_call = 0)
        : MessageStore(flush_thunk_for<FaultStore>()),
          next_inbound_(seeded_inbound),
          next_outbound_(seeded_outbound),
          fail_on_nth_call_(fail_on_nth_call) {}

    // Observable state for witnesses:
    mutable int call_count{0};         // total next_seqnum calls
    seqnum_t durable_inbound{1};       // last persisted inbound counter (increment=true)

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
        direction_t dir, bool increment) noexcept override {
        ++call_count;
        if (fail_on_nth_call_ > 0 && call_count >= fail_on_nth_call_) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        if (increment) {
            if (dir == direction_t::inbound) {
                ++next_inbound_;
                durable_inbound = next_inbound_;
                co_return next_inbound_ - 1U;  // return the pre-increment value
            } else {
                ++next_outbound_;
                co_return next_outbound_ - 1U;
            }
        } else {
            co_return (dir == direction_t::inbound) ? next_inbound_ : next_outbound_;
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_inbound_ = 1;
        next_outbound_ = 1;
        durable_inbound = 1;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    seqnum_t next_inbound_;
    seqnum_t next_outbound_;
    int fail_on_nth_call_;  // 0 = never fail
};

// FaultStoreFactory: wraps a FaultStore for use with SessionConfig.
// NOTE: yields_persistent_store() is NOT overridden here (T004 adds it).
// This factory uses the base-class default (which is true in the post-T004 world).
// For Phase 1 we omit the override entirely since MessageStoreFactory::yields_persistent_store()
// does not exist yet — it is added in T004.
// TODO(T004): override yields_persistent_store() once MessageStoreFactory gains the method.
class FaultStoreFactory final : public MessageStoreFactory {
public:
    explicit FaultStoreFactory(seqnum_t seeded_inbound = 1, seqnum_t seeded_outbound = 1,
                               int fail_on_nth_call = 0)
        : seeded_inbound_(seeded_inbound),
          seeded_outbound_(seeded_outbound),
          fail_on_nth_call_(fail_on_nth_call) {}

    // The last store minted by make() — for observable state in witnesses.
    mutable FaultStore* last_store{nullptr};

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/,
        std::pmr::memory_resource* /*mr*/, std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto store = std::make_unique<FaultStore>(seeded_inbound_, seeded_outbound_,
                                                  fail_on_nth_call_);
        last_store = store.get();
        return store;
    }

private:
    seqnum_t seeded_inbound_;
    seqnum_t seeded_outbound_;
    int fail_on_nth_call_;
};

// ── Session fixture ───────────────────────────────────────────────────────────

struct Fixture {
    asio::io_context ioc;
    OutboundCapture capture;
    fixpp::core::EngineConfig eng;
    fixpp::session::SessionConfig cfg;
    std::unique_ptr<fixpp::session::Session> session;

    void feed(const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, session->on_inbound_frame(std::span<const std::byte>(frame)),
                                  asio::use_future);
        ioc.run_for(5s);
        ioc.restart();
        (void)fut.get();
    }

    void clear_capture() { capture.frames.clear(); }
};

// make_acceptor: build an acceptor Session through its Logon handshake (Active state).
static std::unique_ptr<Fixture> make_acceptor(
    std::shared_ptr<MessageStoreFactory> store_factory, std::uint32_t peer_logon_seq = 1,
    bool enable_789 = false, bool reset_on_logon = false,
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
    fix->cfg.enable_next_expected_msg_seq_num = enable_789;
    fix->cfg.reset_on_logon = reset_on_logon;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    if (app) {
        fix->eng.application = std::move(app);
    }

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    // open() — sets NotConnected for acceptor.
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon to reach Active.
    fix->feed(make_logon("FIX.4.4", peer_logon_seq, "CLI", "SRV"));

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "make_acceptor: session must be Active after Logon";

    return fix;
}

// make_initiator: build an initiator Session, call open() to emit the outbound Logon
// (which transitions the session to LogonSent). The outbound capture holds
// exactly the Logon frame after open().
static std::unique_ptr<Fixture> make_initiator(
    std::shared_ptr<MessageStoreFactory> store_factory, bool enable_789 = false,
    bool reset_on_logon = false,
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
    fix->cfg.enable_next_expected_msg_seq_num = enable_789;
    fix->cfg.reset_on_logon = reset_on_logon;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    if (app) {
        fix->eng.application = std::move(app);
    }

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    // open() emits the initiator Logon and transitions to LogonSent.
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent)
        << "make_initiator: session must be LogonSent after open()";

    return fix;
}

// ── Phase 1 Setup: skeleton build smoke ──────────────────────────────────────
//
// This test exists only to confirm the skeleton compiles, links, and ctest
// discovers the target. All real witnesses (W1–W14) are added in Phases 3–5.

TEST(PersistentSeqnumHydrate, SkeletonBuilds) {
    // Placeholder: confirms the skeleton compiles + links + ctest registers
    // the target. Real assertions are added in later phases (T006/T008/T009/T012).
    SUCCEED();
}

}  // namespace
