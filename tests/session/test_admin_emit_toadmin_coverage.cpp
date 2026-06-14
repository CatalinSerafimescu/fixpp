// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_admin_emit_toadmin_coverage.cpp
//
// 036-admin-emit-toadmin-coverage — FR-001/FR-002/FR-003/FR-006
//
// Exhaustive toAdmin coverage for the 9 admin Reject + Guard-3 Logout emit sites.
//
// Invocation 1 cells (T004, T013): emit_session_reject_ + Guard-3 Logout.
// Invocation 2 cells (T005-T012): remaining 7 admin Reject sites + FR-006 no-app cell.
//
// TDD ordering per [const §VII.3]:
//   T005 GREEN throughout (no-app path; fire_to_admin_ no-ops when app==nullptr).
//   T006/T010 RED at delta==1 (paired Logout already wired; Reject not yet).
//   T007/T008/T009/T011/T012 RED at delta==0 before wiring.
//
// Anchors:
//   spec.md FR-001/FR-002/FR-003/FR-006; plan.md ARM-1; quickstart.md §Per-site cells;
//   session.cpp:331 (fire_to_admin_); session.cpp:1736 (emit_session_reject_);
//   session.cpp:2407 (Q3 Reject); session.cpp:2491 (SeqReset veto Reject);
//   session.cpp:2606 (021ArmC Reject); session.cpp:2651 (021RC1 Reject);
//   session.cpp:2682 (021ArmD Reject); session.cpp:2953 (Logout veto Reject);
//   session.cpp:4589 (SeqReset too-low Reject); [const §VII.3];
//   [feedback_admin_emit_bypasses_fire_to_admin]

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
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

using namespace std::chrono_literals;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::Application;
using fixpp::session::SessionId;
using fixpp::wire::MessageView;
using fixpp::wire::access_mode;

namespace fixpp::session::test {
namespace {

// ── CoverageApp — counting + throwing + vetoing Application ──────────────────
//
// Per quickstart.md §The test double.
// fromAdmin_rejects controls whether fromAdmin returns app_do_not_send (true)
// or accepts (false). Defaults to false so cells that trigger Reject via OTHER
// mechanisms (Q3/021/NewSeqNo-too-low) don't accidentally hit the fromAdmin-veto
// Reject path instead (wrong matrix row — anti-pattern: right end-state via wrong row).
// T004/T007/T011 set fromAdmin_rejects=true explicitly.
//
// toAdmin is inspect-only (FR-008). The throw arm (US2 cells) is gated by
// throw_on_msg_type to prevent the throw from firing during handshake Logon
// (35=A) — without the gate, Mode::Throw breaks open_*_to_active() BEFORE the
// test provocation runs. Each US2 cell sets throw_on_msg_type to the MsgType
// of the specific frame its 036 site will emit (e.g. "3" for Reject cells,
// "5" for the Guard-3 Logout cell).
//
// T011 (LogoutVeto) throw note: the veto-Reject is emitted AFTER the confirming
// Logout (35=5). Setting throw_on_msg_type="3" lets the confirming Logout pass
// and causes the throw on the Reject — discriminating the 036 site at :2953.

struct CoverageApp : Application {
    int toAdmin_calls = 0;
    int toApp_calls   = 0;

    // Per-instance control: whether fromAdmin rejects.
    // Default false so cells that use fromAdmin-ACCEPT paths don't accidentally
    // route through the fromAdmin-veto Reject site at session.cpp:3026.
    bool fromAdmin_rejects = false;

    // US3: whether fromApp rejects (app_do_not_send) to provoke a BMR(35=j).
    // Independent of mode (which governs toApp). Default false.
    bool fromApp_rejects = false;

    // US2 throw gate: Mode::Throw fires toAdmin throw only when
    // msg.msg_type() == throw_on_msg_type. Empty string = throw on every call
    // (safe only when no handshake is needed; prefer the MsgType gate).
    // Anchors: [feedback_noexcept_boundary_user_callback_terminate] (throw caught
    //          INSIDE invoke_callback_safe / fire_to_admin_ noexcept body).
    std::string throw_on_msg_type;

    enum class Mode { Count, Throw, Veto } mode = Mode::Count;

    // fromAdmin: conditionally veto (app_do_not_send) or accept ({}).
    expected_t<void> fromAdmin(const MessageView<access_mode::Index>& /*mv*/,
                               const SessionId& /*id*/) override {
        if (fromAdmin_rejects) return std::unexpected(error::app_do_not_send);
        return {};
    }

    // fromApp: conditionally veto to provoke the BMR(35=j) engine path.
    // mode does NOT affect fromApp — it governs toApp only.
    expected_t<void> fromApp(const MessageView<access_mode::Index>& /*mv*/,
                             const SessionId& /*id*/) override {
        if (fromApp_rejects) return std::unexpected(error::app_do_not_send);
        return {};
    }

    void toAdmin(const MessageView<access_mode::Index>& mv,
                 const SessionId& /*id*/) override {
        ++toAdmin_calls;
        if (mode == Mode::Throw) {
            // Gate the throw by MsgType so the handshake Logon (35=A) is not
            // affected: throw only when this frame's 35= matches throw_on_msg_type.
            // An empty throw_on_msg_type means "throw on any MsgType" — use with
            // care (only when no preceding admin frames can reach toAdmin).
            if (throw_on_msg_type.empty() || mv.msg_type() == throw_on_msg_type) {
                throw std::runtime_error("toAdmin boom");
            }
        }
    }

    expected_t<void> toApp(const MessageView<access_mode::Index>& /*mv*/,
                           const SessionId& /*id*/) override {
        ++toApp_calls;
        if (mode == Mode::Throw) throw std::runtime_error("toApp boom");
        if (mode == Mode::Veto)  return std::unexpected(error::app_do_not_send);
        return {};
    }
};

// ── ObservableStore — minimal persistent store for T028 (BMR veto persist witness) ──
//
// Tracks:
//   durable_inbound:      counter after last next_seqnum(inbound, increment=true).
//   outbound_write_count: number of next_seqnum(outbound, increment=true) calls.
// yields_persistent_store() returns true so the session calls persist_inbound_advance_().
//
// Minimal: only implements the seqnum path; store() and retrieve() are no-ops.
// Pattern mirrors FaultStore from test_persistent_seqnum_hydrate.cpp.
//
// Anchors: C2/INV-COV-5 (veto must still persist inbound); session.cpp:676
//          (persist_inbound_advance_ only writes when store_is_persistent_==true).

using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

class ObservableStore final : public MessageStore {
public:
    explicit ObservableStore(seqnum_t seed_inbound = 1, seqnum_t seed_outbound = 1)
        : MessageStore(flush_thunk_for<ObservableStore>()),
          next_inbound_(seed_inbound),
          next_outbound_(seed_outbound),
          durable_inbound(seed_inbound),
          durable_outbound(seed_outbound) {}

    // Observable state for witnesses.
    seqnum_t durable_inbound;    // updated by next_seqnum(inbound, true)
    seqnum_t durable_outbound;   // updated by next_seqnum(outbound, true)
    int outbound_write_count{0}; // number of outbound increment calls

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
        if (increment) {
            if (dir == direction_t::inbound) {
                ++next_inbound_;
                durable_inbound = next_inbound_;
                co_return next_inbound_ - 1U;
            } else {
                ++outbound_write_count;
                ++next_outbound_;
                durable_outbound = next_outbound_;
                co_return next_outbound_ - 1U;
            }
        }
        co_return (dir == direction_t::inbound) ? next_inbound_ : next_outbound_;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_inbound_  = 1;
        next_outbound_ = 1;
        durable_inbound  = 1;
        durable_outbound = 1;
        outbound_write_count = 0;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    seqnum_t next_inbound_;
    seqnum_t next_outbound_;
};

class ObservableStoreFactory final : public MessageStoreFactory {
public:
    // The last store minted by make() — for observable state in witnesses.
    mutable ObservableStore* last_store{nullptr};

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/,
        std::pmr::memory_resource* /*mr*/, std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto store = std::make_unique<ObservableStore>();
        last_store = store.get();
        return store;
    }
};

// ── Frame builder ─────────────────────────────────────────────────────────────

std::vector<std::byte> build_frame(std::string_view msg_type, std::uint32_t seq,
                                   std::string_view sender, std::string_view target,
                                   std::string_view begin_string, std::string_view sending_time,
                                   std::string_view extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) body += std::string(extra_fields);

    std::string hdr;
    hdr += "8=" + std::string(begin_string) + "\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs  = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Valid sending time matching the mock clock (2024-01-01 00:00:00.000 UTC).
static constexpr std::string_view kGoodSendingTime = "20240101-00:00:00.000";
// Stale sending time (epoch — always > 120 s stale vs 2024 clock).
static constexpr std::string_view kStaleSendingTime = "19700101-00:00:00.000";
// Future sending time (2030) — used in ArmD: 122 > 52.
static constexpr std::string_view kFutureSendingTime = "20300101-00:00:00.000";

std::vector<std::byte> build_logon(std::string_view sender, std::string_view target,
                                   std::string_view sending_time, std::uint32_t seq = 1,
                                   int heartbt = 0) {
    std::string extra = "98=0\x01" "108=" + std::to_string(heartbt) + "\x01";
    return build_frame("A", seq, sender, target, "FIX.4.2", sending_time, extra);
}

// Valid Heartbeat (used in T004 to provoke fromAdmin veto).
std::vector<std::byte> build_heartbeat(std::uint32_t seq, std::string_view sender,
                                       std::string_view target) {
    return build_frame("0", seq, sender, target, "FIX.4.2", kGoodSendingTime);
}

// SequenceReset(35=4) with NewSeqNo(36). GapFillFlag(123)=Y only when gap_fill.
std::vector<std::byte> build_sequence_reset(std::uint32_t seq, std::string_view sender,
                                            std::string_view target, std::uint32_t new_seqno,
                                            bool gap_fill,
                                            std::string_view sending_time = kGoodSendingTime) {
    std::string extra;
    if (gap_fill) extra += "123=Y\x01";
    extra += "36=" + std::to_string(new_seqno) + "\x01";
    return build_frame("4", seq, sender, target, "FIX.4.2", sending_time, extra);
}

// Logout(35=5).
std::vector<std::byte> build_logout_frame(std::uint32_t seq, std::string_view sender,
                                          std::string_view target) {
    return build_frame("5", seq, sender, target, "FIX.4.2", kGoodSendingTime);
}

// App-type frame (35=D, NewOrderSingle) — used to provoke fromApp reject → BMR(35=j).
// The session dispatches fromApp for app-typed messages when Application is registered.
std::vector<std::byte> build_app_frame(std::uint32_t seq, std::string_view sender,
                                       std::string_view target) {
    return build_frame("D", seq, sender, target, "FIX.4.2", kGoodSendingTime);
}

// PossDup Heartbeat(35=0): 43=Y + optional 122= field.
// absent_122=true → omit 122 (Arm C trigger). extra_122 can override the 122 value.
std::vector<std::byte> build_possdup_heartbeat(std::uint32_t seq, std::string_view sender,
                                               std::string_view target,
                                               bool absent_122,
                                               std::string_view orig_sending_time = {}) {
    std::string extra = "43=Y\x01";
    if (!absent_122) {
        extra += "122=";
        extra += (orig_sending_time.empty() ? kGoodSendingTime : orig_sending_time);
        extra += "\x01";
    }
    return build_frame("0", seq, sender, target, "FIX.4.2", kGoodSendingTime, extra);
}

// ── Extract helpers ───────────────────────────────────────────────────────────

static std::string extract_field_str(std::span<const std::byte> frame, int tag) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    std::string needle = std::to_string(tag) + "=";
    auto pos = wire.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    auto end = wire.find('\x01', pos);
    if (end == std::string::npos) return {};
    return wire.substr(pos, end - pos);
}

static std::string extract_msg_type(std::span<const std::byte> frame) {
    return extract_field_str(frame, 35);
}

static std::size_t count_frames_with_type(const std::vector<std::vector<std::byte>>& frames,
                                          std::string_view msg_type) {
    std::size_t n = 0;
    for (const auto& f : frames) {
        if (extract_msg_type(f) == msg_type) ++n;
    }
    return n;
}

// Check whether any captured frame is 35=3 AND 373=<reason>.
static bool any_reject_with_reason(const std::vector<std::vector<std::byte>>& frames,
                                   std::string_view reason) {
    for (const auto& f : frames) {
        if (extract_msg_type(f) == "3" && extract_field_str(f, 373) == reason) return true;
    }
    return false;
}

// ── Fixture ───────────────────────────────────────────────────────────────────

class AdminEmitToAdminCoverageTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    std::shared_ptr<CoverageApp> app;

    std::vector<std::vector<std::byte>> captured_frames;

    static constexpr std::string_view kSender   = "ISLD";
    static constexpr std::string_view kTarget    = "TW";
    static constexpr std::string_view kBeginStr  = "FIX.4.2";

    void SetUp() override {
        using sc = std::chrono::system_clock;
        auto utc = sc::time_point{} + std::chrono::seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();

        app = std::make_shared<CoverageApp>();
        engine.application = app;
    }

    SessionConfig make_acceptor_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string(kSender);
        cfg.target_comp_id = std::string(kTarget);
        cfg.begin_string   = std::string(kBeginStr);
        cfg.heartbeat_interval = 0s;  // disable liveness
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary       = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.role = session_role::acceptor;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    SessionConfig make_initiator_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string(kSender);
        cfg.target_comp_id = std::string(kTarget);
        cfg.begin_string   = std::string(kBeginStr);
        cfg.heartbeat_interval = 0s;  // disable liveness
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary       = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.role = session_role::initiator;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    // Open acceptor: wait for peer Logon → advance to Active.
    // Sender = TW (peer), Target = ISLD (us).
    bool open_acceptor_to_active(Session& sess) {
        if (!open_sync(sess).has_value()) return false;
        auto peer_logon = build_logon(kTarget, kSender, kGoodSendingTime, 1, 0);
        (void)feed_sync(sess, peer_logon);
        return sess.state() == fsm_state::Active;
    }

    // Open initiator: emit our Logon → reach LogonSent.
    bool open_initiator_to_logon_sent(Session& sess) {
        if (!open_sync(sess).has_value()) return false;
        return sess.state() == fsm_state::LogonSent;
    }
};

// ── Fixture for US3 BMR cells (T027/T028/T029) ───────────────────────────────
//
// Uses ObservableStoreFactory (persistent) so the durable inbound seqnum is
// observable for T028 (INV-COV-5 persist-on-veto witness).
// Scoped to BMR cells only — existing cells use the MemoryStore default.
//
// Anchors: C2/INV-COV-5; contracts/admin-emit-coverage.md §C2; quickstart.md §BMR veto cell.

class AdminEmitBMRCoverageTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    std::shared_ptr<CoverageApp> app;
    std::shared_ptr<ObservableStoreFactory> store_factory;

    std::vector<std::vector<std::byte>> captured_frames;

    static constexpr std::string_view kSender  = "ISLD";
    static constexpr std::string_view kTarget   = "TW";
    static constexpr std::string_view kBeginStr = "FIX.4.2";

    void SetUp() override {
        using sc = std::chrono::system_clock;
        auto utc = sc::time_point{} + std::chrono::seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();

        app = std::make_shared<CoverageApp>();
        engine.application = app;

        store_factory = std::make_shared<ObservableStoreFactory>();
    }

    SessionConfig make_acceptor_cfg_with_store() {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string(kSender);
        cfg.target_comp_id = std::string(kTarget);
        cfg.begin_string   = std::string(kBeginStr);
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary       = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.role = session_role::acceptor;
        cfg.store_factory = store_factory;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    // Open acceptor → Active via peer Logon handshake.
    bool open_acceptor_to_active(Session& sess) {
        if (!open_sync(sess).has_value()) return false;
        auto peer_logon = build_logon(kTarget, kSender, kGoodSendingTime, 1, 0);
        (void)feed_sync(sess, peer_logon);
        return sess.state() == fsm_state::Active;
    }

    // Shorthand for the durable inbound counter after the last persist write.
    seqnum_t store_persisted_inbound_seqnum() const {
        return store_factory->last_store ? store_factory->last_store->durable_inbound : 0;
    }

    // Shorthand for outbound write count (0 on veto path).
    int store_outbound_write_count() const {
        return store_factory->last_store ? store_factory->last_store->outbound_write_count : -1;
    }
};

// ── Fixture for T005 — no-app (engine.application == nullptr) ────────────────
// Separate fixture so T005 can set engine.application=nullptr after SetUp.

class AdminEmitNoAppCoverageTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    std::vector<std::vector<std::byte>> captured_frames;

    static constexpr std::string_view kSender   = "ISLD";
    static constexpr std::string_view kTarget    = "TW";

    void SetUp() override {
        using sc = std::chrono::system_clock;
        auto utc = sc::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock    = clock;
        engine.executor = ioc.get_executor();
        engine.application = nullptr;  // no-app path
    }

    SessionConfig make_acceptor_cfg() {
        SessionConfig cfg;
        cfg.sender_comp_id = std::string(kSender);
        cfg.target_comp_id = std::string(kTarget);
        cfg.begin_string   = "FIX.4.2";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary       = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.role = session_role::acceptor;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    expected_t<void> open_sync(Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    expected_t<void> feed_sync(Session& s, std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(200ms);
        ioc.restart();
        return fut.get();
    }

    bool open_acceptor_to_active(Session& sess) {
        if (!open_sync(sess).has_value()) return false;
        // No-app: feed Logon directly (no fromAdmin callback).
        auto peer_logon = build_logon(kTarget, kSender, kGoodSendingTime, 1, 0);
        (void)feed_sync(sess, peer_logon);
        return sess.state() == fsm_state::Active;
    }
};

}  // namespace

// ════════════════════════════════════════════════════════════════════════════
// T004 — EmitSessionReject_FromAdminVeto
//
// Scenario: acceptor Active; fromAdmin veto on inbound Heartbeat →
//   session.cpp:3026 → emit_session_reject_(session.cpp:1736).
// Pre-036: fire_to_admin_ NOT called → toAdmin_calls delta = 0 (RED).
// Post-036: fire_to_admin_ wired (T015) → toAdmin_calls delta = 1 (GREEN).
//
// Anchors: spec.md FR-001/FR-002; plan.md ARM-1; quickstart.md EmitSessionReject row;
//          session.cpp:3026; session.cpp:1736; session.cpp:331 (fire_to_admin_).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, EmitSessionReject_FromAdminVeto) {
    // fromAdmin_rejects=true: this cell's trigger IS the fromAdmin veto.
    app->fromAdmin_rejects = true;

    // Acceptor role: peer=TW sends, we=ISLD receive.
    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active before the provocation";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Snapshot toAdmin_calls and frame count AFTER handshake.
    // The acceptor emits a Logon-reply (35=A) during open_acceptor_to_active,
    // which fires toAdmin for the already-wired Logon site — snapshot excludes it.
    const int toAdmin_before  = app->toAdmin_calls;
    const auto frames_before  = captured_frames.size();

    // Feed a valid Heartbeat from TW. CoverageApp::fromAdmin returns app_do_not_send
    // → session.cpp:3026 → emit_session_reject_.
    // next-expected inbound seq = 2 (peer Logon was seq=1).
    auto hb = build_heartbeat(2, kTarget, kSender);
    (void)feed_sync(sess, hb);

    // Wire: at least one 35=3 (Reject) must have been emitted.
    const auto reject_count = count_frames_with_type(captured_frames, "3");
    EXPECT_GE(reject_count, 1u)
        << "Reject(35=3) must appear on wire after fromAdmin veto "
        << "(frames_before=" << frames_before
        << " total_now=" << captured_frames.size() << ")";

    // toAdmin delta: must be exactly 1 for the Reject frame (FR-001/ARM-1).
    // Pre-036: fire_to_admin_ not called → delta = 0 → this assertion FAILS (RED).
    // Post-036 (after T015): delta = 1 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 1)
        << "toAdmin must fire exactly once for the Reject(35=3) emit "
        << "(delta=" << toAdmin_delta
        << "; toAdmin_before=" << toAdmin_before
        << "; toAdmin_now=" << app->toAdmin_calls << ")";
}

// ════════════════════════════════════════════════════════════════════════════
// T005 — EmitSessionReject_NoAppUnknownType_NoOp
//
// FR-006 byte-identity no-op cell.
// Scenario: acceptor Active, NO application registered; feed app-type MsgType
//   "D" (NewOrderSingle) → session.cpp:3216 → emit_session_reject_(:1736).
//   fire_to_admin_ returns true immediately (engine_.application==nullptr).
//   Result: Reject(35=3) emitted as before 036 — no behaviour change.
//
// This cell is GREEN before and after wiring (no-op). It witnesses FR-006:
//   no-app wire bytes unchanged by the T015 wiring.
// No toAdmin count asserted (application is null — no callback).
//
// Anchors: spec.md FR-006; session.cpp:332 (fire_to_admin_ null-app fast-return);
//          session.cpp:3216 (no-app emit_session_reject_ caller).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitNoAppCoverageTest, EmitSessionReject_NoAppUnknownType_NoOp) {
    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: no-app acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const auto frames_before = captured_frames.size();

    // Feed an app-type frame (35=D, NewOrderSingle) at seq=2.
    // No Application registered → session.cpp:3216 → emit_session_reject_.
    auto app_frame = build_frame("D", 2, kTarget, kSender, "FIX.4.2", kGoodSendingTime);
    (void)feed_sync(sess, app_frame);

    // Wire: a Reject(35=3) must be emitted (no-app pre-036 behaviour preserved).
    const auto reject_count = count_frames_with_type(captured_frames, "3");
    EXPECT_GE(reject_count, 1u)
        << "No-app path must still emit Reject(35=3) for unknown app-type MsgType "
        << "(frames_before=" << frames_before
        << " total_now=" << captured_frames.size() << ")";

    // Session should still be Active (no disconnect from no-app unknown-type).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "No-app unknown MsgType reject must not disconnect the session";
}

// ════════════════════════════════════════════════════════════════════════════
// T006 — Reject_Q3SendingTimeAccuracy
//
// Scenario: acceptor Active (fromAdmin_rejects=false); feed Heartbeat with
//   stale 52= → Guard Q3 fires → Step-1 Reject(:2407) + Step-2 Logout(:2424).
//   Logout step already has fire_to_admin_ from prior wiring.
//   Reject step needs wiring (T006 site).
// Pre-036: Reject fire_to_admin_ NOT called → toAdmin_delta==1 (Logout only) → RED.
// Post-036: Reject + Logout both wired → toAdmin_delta==2 → GREEN.
//
// Anchors: spec.md FR-001/FR-002; session.cpp:2402-2451; plan.md Decision-1 Q3.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Reject_Q3SendingTimeAccuracy) {
    // fromAdmin_rejects=false: Q3 fires on stale 52= BEFORE fromAdmin dispatch.
    // The stale 52 guard runs prior to fromAdmin call, so fromAdmin disposition
    // doesn't affect whether Q3 fires.
    app->fromAdmin_rejects = false;

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;

    // Feed Heartbeat with stale SendingTime → Q3 triggers Reject + Logout + Disconnect.
    // seq=2 (next-expected after Logon).
    auto stale_hb = build_frame("0", 2, kTarget, kSender, "FIX.4.2", kStaleSendingTime);
    (void)feed_sync(sess, stale_hb);

    // Wire: one Reject + one Logout.
    EXPECT_GE(count_frames_with_type(captured_frames, "3"), 1u)
        << "Q3 must emit Reject(35=3)";
    EXPECT_GE(count_frames_with_type(captured_frames, "5"), 1u)
        << "Q3 must emit Logout(35=5)";

    // Session must disconnect.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Q3 must disconnect the session";

    // toAdmin delta must be 2: Reject + Logout.
    // Pre-036: Reject not wired → delta==1 (Logout only) → RED.
    // Post-036: both wired → delta==2 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 2)
        << "toAdmin must fire for both Reject AND Logout in Q3 path "
        << "(delta=" << toAdmin_delta << "; expected 2)";
}

// ════════════════════════════════════════════════════════════════════════════
// T007 — Reject_SequenceResetVeto
//
// Scenario: acceptor Active; fromAdmin_rejects=true; feed SequenceReset(35=4)
//   Reset-mode (123 absent) → fromAdmin veto → Reject(:2491, reason=3).
//   Session survives (best-effort site: co_return ok after emit attempt).
// Pre-036: fire_to_admin_ NOT called → toAdmin_delta==0 → RED.
// Post-036: fire_to_admin_ wired in `if (assign_r)` block → delta==1 → GREEN.
//
// Discriminator: assert 373=3 (InvalidMsgType/Unsupported) NOT 373=5
//   (ValueIsIncorrect). This distinguishes T007 (fromAdmin veto → 2491)
//   from T012 (NewSeqNo too-low → 4589, 373=5).
//
// Anchors: spec.md FR-001/FR-002; session.cpp:2476-2502; plan.md Decision-1 SeqReset-veto.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Reject_SequenceResetVeto) {
    app->fromAdmin_rejects = true;  // veto on the SequenceReset fromAdmin call.

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;

    // Feed SequenceReset Reset-mode (123 absent), NewSeqNo=10.
    // seq=2 (next-expected after Logon); Reset mode bypasses seqnum gate.
    auto sr = build_sequence_reset(50, kTarget, kSender, /*new_seqno=*/10, /*gap_fill=*/false);
    (void)feed_sync(sess, sr);

    // Wire: Reject(35=3) must appear.
    EXPECT_GE(count_frames_with_type(captured_frames, "3"), 1u)
        << "SeqReset fromAdmin veto must emit Reject(35=3)";

    // Discriminator: 373=3 (not 373=5) confirms this is the veto path at :2491,
    // not the NewSeqNo-too-low path at :4589.
    EXPECT_TRUE(any_reject_with_reason(captured_frames, "3"))
        << "SeqReset veto Reject must have SessionRejectReason=3 (site :2491), "
        << "not 373=5 (NewSeqNo-too-low site :4589)";

    // Session must survive (best-effort site returns ok).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "SeqReset fromAdmin veto: session must survive (best-effort emit)";

    // toAdmin delta: 1 (Reject only; no Logout on this path).
    // Pre-036: delta==0 → RED. Post-036: delta==1 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 1)
        << "toAdmin must fire exactly once for the SeqReset veto Reject "
        << "(delta=" << toAdmin_delta << "; expected 1)";
}

// ════════════════════════════════════════════════════════════════════════════
// T008 — Reject_021ArmC_Malformed122 (absent 122)
//
// Scenario: acceptor Active; fromAdmin_rejects=false; feed PossDup Heartbeat
//   (43=Y) with 122 ABSENT → 021 Arm C → Reject(:2606, reason=1 RequiredTagMissing).
//   Session survives (Arm C); does NOT disconnect.
// Pre-036: fire_to_admin_ NOT called → toAdmin_delta==0 → RED.
// Post-036: fire_to_admin_ wired → delta==1 → GREEN.
//
// Anchors: spec.md FR-001/FR-002; session.cpp:2598-2624; plan.md Decision-1 021ArmC.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Reject_021ArmC_Malformed122) {
    // fromAdmin_rejects=false: the reject comes from the absent-122 check, not fromAdmin.
    app->fromAdmin_rejects = false;

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;

    // PossDup Heartbeat: 43=Y, NO 122 field → Arm C (RequiredTagMissing).
    // seq=2 (next-expected after Logon).
    auto frame = build_possdup_heartbeat(2, kTarget, kSender, /*absent_122=*/true);
    (void)feed_sync(sess, frame);

    // Wire: Reject(35=3) must appear.
    EXPECT_GE(count_frames_with_type(captured_frames, "3"), 1u)
        << "021 Arm C must emit Reject(35=3) for absent 122";

    // Session must survive (Arm C does not disconnect).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "021 Arm C: session must survive after Reject";

    // toAdmin delta: 1.
    // Pre-036: delta==0 → RED. Post-036: delta==1 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 1)
        << "toAdmin must fire once for 021 ArmC Reject "
        << "(delta=" << toAdmin_delta << "; expected 1)";
}

// ════════════════════════════════════════════════════════════════════════════
// T009 — Reject_021RC1_Malformed122 (present-but-unparseable 122)
//
// Scenario: acceptor Active; fromAdmin_rejects=false; feed PossDup Heartbeat
//   (43=Y) with 122=GARBAGE (present but unparseable) → 021 RC#1 →
//   Reject(:2651, reason=1 RequiredTagMissing). Session survives.
// Pre-036: fire_to_admin_ NOT called → toAdmin_delta==0 → RED.
// Post-036: fire_to_admin_ wired → delta==1 → GREEN.
//
// Anchors: spec.md FR-001/FR-002; session.cpp:2641-2668; plan.md Decision-1 021RC1;
//          [feedback_conjunctive_parse_guard_tolerates_malformed_field].
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Reject_021RC1_Malformed122) {
    app->fromAdmin_rejects = false;

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;

    // PossDup Heartbeat: 43=Y, 122=GARBAGE (unparseable) → RC#1 → Arm C Reject.
    auto frame = build_possdup_heartbeat(2, kTarget, kSender, /*absent_122=*/false,
                                         /*orig_sending_time=*/"GARBAGE");
    (void)feed_sync(sess, frame);

    // Wire: Reject(35=3) must appear.
    EXPECT_GE(count_frames_with_type(captured_frames, "3"), 1u)
        << "021 RC#1 must emit Reject(35=3) for present-but-unparseable 122";

    // Session must survive.
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "021 RC#1: session must survive after Reject";

    // toAdmin delta: 1.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 1)
        << "toAdmin must fire once for 021 RC#1 Reject "
        << "(delta=" << toAdmin_delta << "; expected 1)";
}

// ════════════════════════════════════════════════════════════════════════════
// T010 — Reject_021ArmD
//
// Scenario: acceptor Active; fromAdmin_rejects=false; feed PossDup Heartbeat
//   (43=Y) with 122 > 52 (strict) → Arm D → Step-1 Reject(:2682) +
//   Step-2 Logout (already wired) + Disconnect.
// Pre-036: Reject fire_to_admin_ NOT called → toAdmin_delta==1 (Logout only) → RED.
// Post-036: both wired → toAdmin_delta==2 → GREEN.
//
// Anchors: spec.md FR-001/FR-002; session.cpp:2671-2724; plan.md Decision-1 021ArmD.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Reject_021ArmD) {
    app->fromAdmin_rejects = false;

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;

    // PossDup Heartbeat: 43=Y, 122=2030 (future > 52=2024-01-01) → Arm D.
    auto frame = build_possdup_heartbeat(2, kTarget, kSender, /*absent_122=*/false,
                                         /*orig_sending_time=*/kFutureSendingTime);
    (void)feed_sync(sess, frame);

    // Wire: Reject + Logout.
    EXPECT_GE(count_frames_with_type(captured_frames, "3"), 1u)
        << "021 Arm D must emit Reject(35=3)";
    EXPECT_GE(count_frames_with_type(captured_frames, "5"), 1u)
        << "021 Arm D must emit Logout(35=5)";

    // Session must disconnect.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "021 Arm D must disconnect the session";

    // toAdmin delta: 2 (Reject + Logout).
    // Pre-036: Reject not wired → delta==1 → RED. Post-036: delta==2 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 2)
        << "toAdmin must fire for both Reject AND Logout in 021 ArmD "
        << "(delta=" << toAdmin_delta << "; expected 2)";
}

// ════════════════════════════════════════════════════════════════════════════
// T011 — Reject_LogoutVeto
//
// Scenario: acceptor Active; fromAdmin_rejects=true; feed Logout(35=5) →
//   Active path first emits a CONFIRMING Logout (35=5 outbound) with
//   fire_to_admin_ already wired (019 T014, session.cpp:2903).
//   THEN: fromAdmin is dispatched → veto → Reject(:2953, best-effort).
//   Session disconnects regardless of fromAdmin result.
//
// Breakdown of toAdmin calls on this path:
//   +1: confirming Logout emit (session.cpp:2903 — already wired pre-036)
//   +1: Reject emit (session.cpp:2953 — needs wiring in this round)
//   Total expected: toAdmin_delta == 2.
//
// Pre-036: Reject(:2953) fire_to_admin_ NOT called →
//          toAdmin_delta==1 (confirming Logout only) → RED.
// Post-036: fire_to_admin_ wired inside `if (assign_r)` block →
//           toAdmin_delta==2 → GREEN.
//
// Anchors: spec.md FR-001/FR-002; session.cpp:2900-2963; plan.md Decision-1 Logout-veto.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Reject_LogoutVeto) {
    app->fromAdmin_rejects = true;  // veto on inbound Logout.

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;

    // Feed Logout from peer.
    // Active path: (1) emits confirming Logout (fire_to_admin_ at :2903 fires toAdmin),
    // (2) dispatches fromAdmin → our veto → Reject(:2953) emitted (best-effort).
    // Session disconnects regardless.
    auto lo = build_logout_frame(2, kTarget, kSender);
    (void)feed_sync(sess, lo);

    // Wire: both confirming Logout AND Reject must appear.
    EXPECT_GE(count_frames_with_type(captured_frames, "5"), 1u)
        << "Logout path must emit confirming Logout(35=5)";
    EXPECT_GE(count_frames_with_type(captured_frames, "3"), 1u)
        << "Logout fromAdmin veto must emit Reject(35=3) (best-effort)";

    // Session disconnects regardless.
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Logout path must disconnect the session";

    // toAdmin delta: 2 (confirming Logout + veto Reject).
    // Pre-036: Reject(:2953) not wired → delta==1 → RED.
    // Post-036: Reject wired → delta==2 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 2)
        << "toAdmin must fire for confirming Logout AND veto Reject "
        << "(delta=" << toAdmin_delta << "; expected 2)";
}

// ════════════════════════════════════════════════════════════════════════════
// T012 — Reject_SeqResetNewSeqNoTooLow
//
// Scenario: acceptor Active; fromAdmin_rejects=false (fromAdmin ACCEPTS the
//   SequenceReset); feed SequenceReset(35=4) Reset-mode with NewSeqNo=1 <
//   next-expected=2 → apply_inbound_sequence_reset → Reject(:4589, reason=5
//   ValueIsIncorrect). Session survives; counter unchanged.
//
// IMPORTANT: fromAdmin_rejects must be FALSE here. If true, the frame routes
//   to the SeqReset fromAdmin veto site at :2491 (T007) INSTEAD of reaching
//   apply_inbound_sequence_reset. That would be the "right end-state via wrong
//   matrix row" false-pass trap.
//
// Pre-036: fire_to_admin_ NOT called → toAdmin_delta==0 → RED.
// Post-036: fire_to_admin_ wired → delta==1 → GREEN.
//
// Discriminator: assert 373=5 (ValueIsIncorrect) — distinguishes this from
//   T007 which emits 373=3 (fromAdmin veto path at :2491).
//
// Anchors: spec.md FR-001/FR-002; session.cpp:4580-4605; plan.md Decision-1 SeqReset-too-low.
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Reject_SeqResetNewSeqNoTooLow) {
    // CRITICAL: fromAdmin_rejects=false so fromAdmin ACCEPTS the SeqReset.
    // This lets the frame fall through to apply_inbound_sequence_reset.
    // If fromAdmin_rejects=true, the test would pass T007's site (:2491)
    // instead of T012's site (:4589) — wrong matrix row.
    app->fromAdmin_rejects = false;

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;

    // SequenceReset Reset-mode (123 absent), NewSeqNo=1 < next-expected=2.
    // Reset mode bypasses the seqnum gate (its own MsgSeqNum is ignored for
    // ordering), so we can use seq=2 and new_seqno=1.
    auto sr = build_sequence_reset(2, kTarget, kSender, /*new_seqno=*/1, /*gap_fill=*/false);
    (void)feed_sync(sess, sr);

    // Wire: Reject(35=3) must appear.
    EXPECT_GE(count_frames_with_type(captured_frames, "3"), 1u)
        << "NewSeqNo too-low must emit Reject(35=3)";

    // Discriminator: 373=5 (ValueIsIncorrect) — MUST not be 373=3.
    // 373=5 confirms the NewSeqNo-too-low path at :4589, not the fromAdmin
    // veto path at :2491 which emits 373=3.
    EXPECT_TRUE(any_reject_with_reason(captured_frames, "5"))
        << "NewSeqNo too-low Reject must have 373=5 (ValueIsIncorrect), "
        << "not 373=3 (which would indicate the wrong :2491 fromAdmin-veto path)";

    // Session must survive (apply_inbound_sequence_reset returns ok after Reject).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "NewSeqNo too-low Reject: session must survive";

    // toAdmin delta: 1.
    // Pre-036: delta==0 → RED. Post-036: delta==1 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 1)
        << "toAdmin must fire once for the NewSeqNo-too-low Reject "
        << "(delta=" << toAdmin_delta << "; expected 1)";
}

// ════════════════════════════════════════════════════════════════════════════
// T013 — Logout_Guard3LogonAckSendingTime
//
// Scenario: initiator LogonSent; peer Logon-ack with stale 52= →
//   session.cpp Guard-3 → build_logout + emit (session.cpp:3368..3380).
// Pre-036: fire_to_admin_ NOT called → toAdmin_calls delta = 0 (RED).
// Post-036: fire_to_admin_ wired (T023) → toAdmin_calls delta = 1 (GREEN).
//
// Anchors: spec.md FR-001/FR-002; plan.md ARM-1; quickstart.md Logout_Guard3 row;
//          session.cpp:3362-3383 (Guard-3 block); session.cpp:331 (fire_to_admin_).
// ════════════════════════════════════════════════════════════════════════════

TEST_F(AdminEmitToAdminCoverageTest, Logout_Guard3LogonAckSendingTime) {
    // Initiator role: we=ISLD send, peer=TW responds.
    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_initiator_to_logon_sent(sess))
        << "Prerequisite: initiator must reach LogonSent before the provocation";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    // Snapshot toAdmin_calls and frame count AFTER our outbound Logon.
    // open() emits the initiator Logon (35=A) which fires toAdmin at the already-wired
    // Logon site — snapshot excludes it.
    const int toAdmin_before  = app->toAdmin_calls;
    const auto frames_before  = captured_frames.size();

    // Feed peer Logon-ack with stale SendingTime → Guard-3 fires.
    // Sender = TW (peer), Target = ISLD (us), seq = 1, heartbt = 0.
    auto stale_logon = build_logon(kTarget, kSender, kStaleSendingTime, 1, 0);
    (void)feed_sync(sess, stale_logon);

    // Wire: at least one 35=5 (Logout) must have been emitted.
    const auto logout_count = count_frames_with_type(captured_frames, "5");
    EXPECT_GE(logout_count, 1u)
        << "Logout(35=5) must appear on wire after Guard-3 SendingTime failure "
        << "(frames_before=" << frames_before
        << " total_now=" << captured_frames.size() << ")";

    // Session must have reached Disconnected (Guard-3 unconditionally disconnects).
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "Session must reach Disconnected after Guard-3 Logout";

    // toAdmin delta: must be exactly 1 for the Logout frame (FR-001/ARM-1).
    // Pre-036: fire_to_admin_ not called → delta = 0 → this assertion FAILS (RED).
    // Post-036 (after T023): delta = 1 → GREEN.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 1)
        << "toAdmin must fire exactly once for the Guard-3 Logout(35=5) emit "
        << "(delta=" << toAdmin_delta
        << "; toAdmin_before=" << toAdmin_before
        << "; toAdmin_now=" << app->toAdmin_calls << ")";
}

// ════════════════════════════════════════════════════════════════════════════
// US2 — Throwing-toAdmin variants (T025 cells)
//
// For each of the 9 admin cells where a registered Application's toAdmin CAN
// throw (T004/T006-T013 in US1), re-run the same provocation with
// CoverageApp::Mode::Throw and throw_on_msg_type set to the MsgType of the
// frame emitted at the 036 site under test.  Assert:
//   - result.error() == app_callback_threw  (the C1 arm returned the error)
//   - sess.state() == fsm_state::Disconnected (arm calls record_state_transition_)
//
// MsgType gate is required because toAdmin also fires for the handshake Logon
// (35=A); without the gate, the throw fires BEFORE the provocation and
// open_acceptor_to_active / open_initiator_to_logon_sent fails (FR-003-shape).
//
// Source-read note (T026):
//   The throw is caught INSIDE invoke_callback_safe (session.hpp:449), which is
//   called from parse_and_dispatch_ (session.cpp:289), called from fire_to_admin_
//   (session.cpp:331). fire_to_admin_ is `noexcept`. The throw NEVER crosses
//   the noexcept boundary — invoke_callback_safe's try/catch converts it to
//   unexpected(app_callback_threw) and returns false. This matches
//   [feedback_noexcept_boundary_user_callback_terminate]: wrap the throw INSIDE
//   the noexcept body (invoke_callback_safe), NOT at the call site.
//
// All 9 cells are GREEN immediately after US1 wiring (the C1 arm is already
// present). The result.error() assertion is the discriminator: without the C1
// arm, on_inbound_frame would terminate() (noexcept violation) rather than
// returning unexpected(app_callback_threw).
// ════════════════════════════════════════════════════════════════════════════

// T025a — EmitSessionReject_FromAdminVeto — throw variant
//
// The 036 site at session.cpp:1736 (emit_session_reject_) emits 35=3.
// Throw on 35=3 → C1 arm → unexpected(app_callback_threw) + Disconnected.
// Anchors: spec.md FR-003; plan.md ARM-1; session.cpp:3026→1736.

TEST_F(AdminEmitToAdminCoverageTest, EmitSessionReject_FromAdminVeto_Throw) {
    app->fromAdmin_rejects    = true;
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";  // gate: throw only on Reject, not on handshake Logon

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active (handshake Logon 35=A passes gate)";

    auto hb = build_heartbeat(2, kTarget, kSender);
    auto result = feed_sync(sess, hb);

    EXPECT_FALSE(result.has_value())
        << "on_inbound_frame must return an error when toAdmin throws";
    EXPECT_EQ(result.error(), error::app_callback_threw)
        << "C1 arm must surface app_callback_threw (not terminate)";
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "C1 arm must disconnect the session";
}

// T025b — Reject_Q3SendingTimeAccuracy — throw variant
//
// The 036 site at session.cpp:2407 (Q3 Reject) emits 35=3 (Step-1).
// Throw on 35=3 → C1 arm fires at Step-1; Step-2 Logout is never reached.
// Anchors: spec.md FR-003; session.cpp:2407.

TEST_F(AdminEmitToAdminCoverageTest, Reject_Q3SendingTimeAccuracy_Throw) {
    app->fromAdmin_rejects    = false;
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess));

    auto stale_hb = build_frame("0", 2, kTarget, kSender, "FIX.4.2", kStaleSendingTime);
    auto result = feed_sync(sess, stale_hb);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// T025c — Reject_SequenceResetVeto — throw variant
//
// The 036 site at session.cpp:2491 (SeqReset veto Reject) emits 35=3.
// Best-effort site (inside `if (assign_r)`); throw → C1 arm → Disconnected.
// Anchors: spec.md FR-003; session.cpp:2491.

TEST_F(AdminEmitToAdminCoverageTest, Reject_SequenceResetVeto_Throw) {
    app->fromAdmin_rejects    = true;
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess));

    auto sr = build_sequence_reset(50, kTarget, kSender, /*new_seqno=*/10, /*gap_fill=*/false);
    auto result = feed_sync(sess, sr);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// T025d — Reject_021ArmC_Malformed122 — throw variant
//
// The 036 site at session.cpp:2606 (021 Arm C Reject) emits 35=3.
// Throw on 35=3 → C1 arm → Disconnected.
// Anchors: spec.md FR-003; session.cpp:2606.

TEST_F(AdminEmitToAdminCoverageTest, Reject_021ArmC_Malformed122_Throw) {
    app->fromAdmin_rejects    = false;
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess));

    auto frame = build_possdup_heartbeat(2, kTarget, kSender, /*absent_122=*/true);
    auto result = feed_sync(sess, frame);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// T025e — Reject_021RC1_Malformed122 — throw variant
//
// The 036 site at session.cpp:2651 (021 RC#1 Reject) emits 35=3.
// Throw on 35=3 → C1 arm → Disconnected.
// Anchors: spec.md FR-003; session.cpp:2651.

TEST_F(AdminEmitToAdminCoverageTest, Reject_021RC1_Malformed122_Throw) {
    app->fromAdmin_rejects    = false;
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess));

    auto frame = build_possdup_heartbeat(2, kTarget, kSender, /*absent_122=*/false,
                                         /*orig_sending_time=*/"GARBAGE");
    auto result = feed_sync(sess, frame);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// T025f — Reject_021ArmD — throw variant
//
// The 036 site at session.cpp:2682 (021 Arm D Reject) emits 35=3.
// Throw on 35=3 → C1 arm fires at Step-1; Step-2 Logout (35=5) never reached.
// Anchors: spec.md FR-003; session.cpp:2682.

TEST_F(AdminEmitToAdminCoverageTest, Reject_021ArmD_Throw) {
    app->fromAdmin_rejects    = false;
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess));

    auto frame = build_possdup_heartbeat(2, kTarget, kSender, /*absent_122=*/false,
                                         /*orig_sending_time=*/kFutureSendingTime);
    auto result = feed_sync(sess, frame);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// T025g — Reject_LogoutVeto — throw variant
//
// Path on inbound Logout (Active):
//   Step-1: confirming Logout (35=5) emitted → fire_to_admin_ → toAdmin fires.
//           throw_on_msg_type="3" → gate PASSES (35=5 ≠ "3") → no throw.
//   Step-2: fromAdmin dispatched → veto → Reject (35=3) emitted → fire_to_admin_
//           → toAdmin fires → gate MATCHES (35=3 == "3") → THROW.
//   C1 arm: record_state_transition_(Disconnected) + co_return unexpected(app_callback_threw).
//
// Discriminating: the confirming Logout (35=5) is allowed through; the throw
// fires at the 036 site (:2953) on the Reject. Without the 036 wiring at :2953,
// the Reject's fire_to_admin_ is never called and the throw would not occur on
// this frame → the test correctly witnesses the 036 C1 arm at :2953.
//
// Anchors: spec.md FR-003; session.cpp:2953 (036 T011 site).

TEST_F(AdminEmitToAdminCoverageTest, Reject_LogoutVeto_Throw) {
    app->fromAdmin_rejects    = true;   // veto on inbound Logout → triggers the Reject
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";    // throw on Reject; confirming Logout (35=5) passes

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess));

    auto lo = build_logout_frame(2, kTarget, kSender);
    auto result = feed_sync(sess, lo);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// T025h — Reject_SeqResetNewSeqNoTooLow — throw variant
//
// The 036 site at session.cpp:4589 (apply_inbound_sequence_reset too-low Reject)
// emits 35=3. Throw on 35=3 → C1 arm → Disconnected.
// fromAdmin_rejects=false so the frame reaches apply_inbound_sequence_reset
// (not the fromAdmin-veto path at :2491).
// Anchors: spec.md FR-003; session.cpp:4589.

TEST_F(AdminEmitToAdminCoverageTest, Reject_SeqResetNewSeqNoTooLow_Throw) {
    app->fromAdmin_rejects    = false;
    app->mode                 = CoverageApp::Mode::Throw;
    app->throw_on_msg_type    = "3";

    auto cfg = make_acceptor_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess));

    auto sr = build_sequence_reset(2, kTarget, kSender, /*new_seqno=*/1, /*gap_fill=*/false);
    auto result = feed_sync(sess, sr);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// T025i — Logout_Guard3LogonAckSendingTime — throw variant
//
// The 036 site at session.cpp:3368 (Guard-3 Logout) emits 35=5.
// Initiator path: our outbound Logon (35=A) fires toAdmin first (via the
// existing Logon site). Gate throw_on_msg_type="5" — Logon (35=A) passes;
// throw fires when Guard-3 Logout (35=5) calls toAdmin.
// C1 arm: record_state_transition_(Disconnected) + co_return unexpected(app_callback_threw).
// Anchors: spec.md FR-003; session.cpp:3368-3380 (Guard-3 block).

TEST_F(AdminEmitToAdminCoverageTest, Logout_Guard3LogonAckSendingTime_Throw) {
    app->mode              = CoverageApp::Mode::Throw;
    app->throw_on_msg_type = "5";   // throw on Logout; initiator Logon (35=A) passes

    auto cfg = make_initiator_cfg();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_initiator_to_logon_sent(sess))
        << "Prerequisite: initiator must reach LogonSent";

    auto stale_logon = build_logon(kTarget, kSender, kStaleSendingTime, 1, 0);
    auto result = feed_sync(sess, stale_logon);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), error::app_callback_threw);
    EXPECT_EQ(sess.state(), fsm_state::Disconnected);
}

// ════════════════════════════════════════════════════════════════════════════
// US3 — BusinessMessageReject(35=j) through toApp (ARM 2)
//
// Anchors: spec.md FR-004/FR-005; contracts/admin-emit-coverage.md C2/C3;
//          data-model.md INV-COV-2/INV-COV-5; quickstart.md §BMR veto cell;
//          session.cpp:3288-3307 (BMR site); session.cpp:3320 (persist_inbound_advance_).
// ════════════════════════════════════════════════════════════════════════════

// T027 — BMR_ToApp_Observed
//
// Scenario: acceptor Active; fromApp_rejects=true → session builds BMR(35=j) →
//   C2 arm: toApp callback fires before assign_outbound → frame transmitted on wire.
// Assert:
//   toApp_calls == 1              (C2 wired — callback fires)
//   toAdmin_calls delta == 0      (INV-COV-2: 35=j routes through toApp, NOT toAdmin)
//   one 35=j frame on wire        (non-veto path: frame stored+emitted)
//
// Pre-036: toApp callback NOT called → toApp_calls==1 FAILS (RED — discriminating RED).
// Post-036: C2 wired → GREEN.
//
// Anchors: FR-004/C2; data-model.md INV-COV-2; quickstart.md BMR_ToApp_Observed row.

TEST_F(AdminEmitBMRCoverageTest, BMR_ToApp_Observed) {
    app->fromApp_rejects = true;  // provoke BMR: fromApp returns app_do_not_send.
    // mode == Mode::Count (default): toApp counts, neither vetos nor throws.

    auto cfg = make_acceptor_cfg_with_store();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    const int toAdmin_before = app->toAdmin_calls;
    // Capture in-memory outbound seqnum before provocation: assign_outbound() on the
    // non-veto path must consume exactly one outbound seqnum.
    const seqnum_t outbound_seq_before = sess.seqnum_mgr_test_access().peek_outbound();

    // Feed app-type frame (35=D) at seq=2. fromApp rejects → BMR(35=j) emitted.
    auto app_frame = build_app_frame(2, kTarget, kSender);
    (void)feed_sync(sess, app_frame);

    // toApp must be called exactly once for the BMR(35=j).
    // Pre-036: toApp_calls==0 → FAILS (RED). Post-036: toApp_calls==1 → GREEN.
    EXPECT_EQ(app->toApp_calls, 1)
        << "toApp must fire exactly once for BMR(35=j) emit "
        << "(toApp_calls=" << app->toApp_calls << "; expected 1)";

    // INV-COV-2: 35=j routes through toApp, NOT toAdmin.
    const int toAdmin_delta = app->toAdmin_calls - toAdmin_before;
    EXPECT_EQ(toAdmin_delta, 0)
        << "toAdmin must NOT fire for BMR(35=j) (INV-COV-2: app message, not admin) "
        << "(delta=" << toAdmin_delta << "; expected 0)";

    // Non-veto path: 35=j must appear on wire.
    EXPECT_EQ(count_frames_with_type(captured_frames, "j"), 1u)
        << "Non-veto path: one BMR(35=j) must be on wire";

    // Session stays Active after a BMR emit.
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "Session must remain Active after BMR emit (fromApp veto only rejects the app msg)";

    // Non-veto path: outbound seqnum must have been consumed (assign_outbound called).
    // Lock this so T028's "seqnum unchanged" assertion is discriminating (not vacuous).
    const seqnum_t outbound_seq_after = sess.seqnum_mgr_test_access().peek_outbound();
    EXPECT_EQ(outbound_seq_after, outbound_seq_before + 1)
        << "Non-veto path: assign_outbound must advance the outbound seqnum by 1 "
        << "(before=" << outbound_seq_before << " after=" << outbound_seq_after << ")";
}

// T028 — BMR_VetoSuppressed_PersistStillFires
//
// THE discriminating witness for INV-COV-5 (the under-persist hazard).
// Scenario: acceptor Active; fromApp_rejects=true; mode=Veto → toApp returns
//   app_do_not_send → C2 suppressed path: BMR(35=j) NOT emitted; NO outbound
//   seqnum consumed; session stays Active; inbound durable persist STILL fires.
//
// Assert:
//   toApp_calls == 1               (toApp fires before veto decision)
//   no 35=j on wire               (suppressed — not stored/emitted)
//   session Active                 (stays alive; BMR veto is not a fatal event)
//   outbound_write_count == 0      (no outbound seqnum consumed)
//   durable_inbound == before + 1  (CRITICAL INV-COV-5: persist fires despite veto)
//
// The persist-on-veto assertion is the discriminating witness: an impl that
// co_returns on veto (skipping persist_inbound_advance_()) passes the first 4
// but FAILS on durable_inbound. Use persistent ObservableStore to observe.
//
// Anchors: FR-004; C2/INV-COV-5; quickstart.md §BMR veto cell;
//          session.cpp:3320 (persist_inbound_advance_ outside fromApp branch).

TEST_F(AdminEmitBMRCoverageTest, BMR_VetoSuppressed_PersistStillFires) {
    app->fromApp_rejects = true;   // provoke BMR
    app->mode = CoverageApp::Mode::Veto;  // toApp vetoes: app_do_not_send

    auto cfg = make_acceptor_cfg_with_store();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Snapshot: after Logon handshake, durable_inbound is 2 (Logon was seq=1,
    // then persist_inbound_advance_ set durable to 2).
    // Capture before the provocation to measure the delta.
    const seqnum_t inbound_durable_before = store_persisted_inbound_seqnum();
    // Capture in-memory outbound seqnum. On veto, assign_outbound() must NOT be
    // called — peek_outbound() must stay the same after the provocation.
    // This is discriminating: T027 proves that on the non-veto path peek_outbound()
    // DOES advance by 1; so "unchanged here" is a real test, not a vacuous pass.
    const seqnum_t outbound_seq_before = sess.seqnum_mgr_test_access().peek_outbound();

    // Feed app-type frame (35=D) at seq=2. fromApp rejects → toApp vetoes BMR.
    auto app_frame = build_app_frame(2, kTarget, kSender);
    (void)feed_sync(sess, app_frame);

    // toApp must fire exactly once (the callback IS invoked, then decides to veto).
    EXPECT_EQ(app->toApp_calls, 1)
        << "toApp must fire once even on the veto path (before veto decision)";

    // 35=j must NOT be on wire (suppressed by veto).
    EXPECT_EQ(count_frames_with_type(captured_frames, "j"), 0u)
        << "Veto: BMR(35=j) must be suppressed (not emitted)";

    // Session must remain Active (veto is not a fatal outcome).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "Veto: session must stay Active";

    // No outbound seqnum consumed on veto path (C2: assign_outbound skipped on suppressed).
    // Discriminating: T027 proves peek_outbound advances by 1 on non-veto; here it must NOT.
    const seqnum_t outbound_seq_after = sess.seqnum_mgr_test_access().peek_outbound();
    EXPECT_EQ(outbound_seq_after, outbound_seq_before)
        << "Veto: outbound seqnum must NOT be consumed (assign_outbound skipped) "
        << "(before=" << outbound_seq_before << " after=" << outbound_seq_after << ")";

    // CRITICAL (INV-COV-5): the veto suppresses the BMR emit but must NOT skip
    // persist_inbound_advance_(). If the impl early-returns on veto, durable_inbound
    // stays at before → restart would reprocess the message (under-persist silent loss,
    // inverted [[feedback_unconditional_persist_at_multiexit_gate_breaks_lowerbound]]).
    EXPECT_EQ(store_persisted_inbound_seqnum(), inbound_durable_before + 1)
        << "Veto: inbound durable seqnum MUST still advance (persist_inbound_advance_() "
        << "must not be skipped by the veto) — INV-COV-5 "
        << "(before=" << inbound_durable_before
        << " after=" << store_persisted_inbound_seqnum() << "; expected " << (inbound_durable_before + 1) << ")";
}

// T029 — BMR_Throw_TerminalClose
//
// Scenario: acceptor Active; fromApp_rejects=true; mode=Throw → toApp throws →
//   C2 throw arm: terminal close + app_callback_threw.
// Note: throw_on_msg_type is NOT set on toAdmin (only toApp can throw here).
//   Set throw_on_msg_type="j" on toAdmin (no-op: toAdmin never fires for 35=j
//   per INV-COV-2) and leave toApp throw ungated (toApp has no MsgType gate —
//   only Mode::Throw controls it). Actually: toAdmin IS gated by throw_on_msg_type;
//   set it to "j" so the handshake Logon (35=A) fires toAdmin WITHOUT throwing.
//
// Assert: result.error() == app_callback_threw; sess.state() == Disconnected.
// No durable_inbound assertion: terminal close → persist is moot (C2 spec).
//
// Anchors: FR-003; C2 (throw arm → terminal close, persist moot);
//          session.cpp:3277-3279 (fromApp throw close pattern mirror).

TEST_F(AdminEmitBMRCoverageTest, BMR_Throw_TerminalClose) {
    app->fromApp_rejects   = true;        // provoke BMR
    app->mode              = CoverageApp::Mode::Throw;  // toApp throws
    // Gate toAdmin throw by MsgType="j" — no admin frame is type j, so the
    // handshake Logon (35=A) passes toAdmin without throwing, while toApp still
    // throws unconditionally (toApp has no MsgType gate in CoverageApp).
    app->throw_on_msg_type = "j";

    auto cfg = make_acceptor_cfg_with_store();
    Session sess(engine, cfg);

    ASSERT_TRUE(open_acceptor_to_active(sess))
        << "Prerequisite: acceptor must reach Active";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Feed app-type frame (35=D) at seq=2. fromApp rejects → toApp throws → C2 throw arm.
    auto app_frame = build_app_frame(2, kTarget, kSender);
    auto result = feed_sync(sess, app_frame);

    EXPECT_FALSE(result.has_value())
        << "on_inbound_frame must return an error when toApp throws";
    EXPECT_EQ(result.error(), error::app_callback_threw)
        << "C2 throw arm must surface app_callback_threw";
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "C2 throw arm must close the session terminally";
}

}  // namespace fixpp::session::test
