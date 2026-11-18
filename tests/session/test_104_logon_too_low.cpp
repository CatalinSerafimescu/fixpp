// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_104_logon_too_low.cpp — test-only.
//
// Feature 104 (specs/104-sequence-reset-fidelity/), fixpp#701: in LogonSent and in NotConnected,
// a Logon whose MsgSeqNum is below NextNumIn draws QuickFIX's too-low Logout, "MsgSeqNum too
// low, expecting N but received M", whatever its PossDupFlag(43), once the identity gates have
// passed; then Disconnected (research D5, D6; contract rows T1 to T8). The cells are research
// D10's T-1 to T-6 (tasks.md T078 to T081).
//
// They run on resend_gap_scene.hpp's Scene, whose first session is replaced before it opens, so
// the cell's session is built from the edited config. The initiator's acknowledgement carries no
// 141=Y, so it takes the sequence check (an unasked 141=Y acknowledgement takes none, US2).
//
// The store is a HookedStore over a MemoryStore hydrated at NextNumIn kN and next outbound kOut,
// distinct from each other and from the frame's kM, so a swapped number cannot pass. A cell that
// needs a store the session does not hydrate from wraps the same HookedStore in a factory whose
// yields_persistent_store() is false, and sets the manager's numbers through
// seqnum_manager_test_access. The text a cell expects is spelled out here with std::to_string,
// never read from the session.

#include <gtest/gtest.h>

#include <array>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/error.hpp>
#include <fixpp/core/fix_time.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/logon_credentials.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "session/resend_gap_scene.hpp"
#include "support/hooked_store.hpp"
#include "support/seqnum_manager_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/transport_double.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::reset_seqnum_policy;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::test_support::HookedStore;
using fixpp::test_support::HookedStoreFactory;
using fixpp::test_support::StoreLog;

// NextNumIn, the frame's MsgSeqNum, and the next outbound number the store holds before open().
constexpr seqnum_t kN = 5;
constexpr seqnum_t kM = 3;
constexpr seqnum_t kOut = 8;
static_assert(kM < kN && kN != kOut && kM != kOut && kOut + 1 != kN);

// refuse_logon_with_logout_'s buffer, spelled out (research D5, "The helper").
constexpr std::size_t kLogoutBufferBytes = 256;

[[nodiscard]] std::string too_low_text(seqnum_t n, seqnum_t m) {
    return "MsgSeqNum too low, expecting " + std::to_string(n) + " but received " +
           std::to_string(m);
}

[[nodiscard]] char const* role_name(session_role r) {
    return r == session_role::initiator ? "initiator (LogonSent)" : "acceptor (NotConnected)";
}

// A frame from the peer `peer` to `us`: 35, 34, 49, 52, 56, then `extra`.
std::vector<std::byte> peer_frame(std::string_view peer, std::string_view us,
                                  std::string_view msg_type, seqnum_t seq,
                                  std::string_view sending_time, std::string_view extra) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(peer) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(us) + "\x01";
    body += std::string(extra);
    std::string full =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char const c : full) cs += c;
    std::array<char, 8> csbuf{};
    std::snprintf(csbuf.data(), csbuf.size(), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf.data()) + "\x01";
    std::vector<std::byte> out;
    out.reserve(full.size());
    for (char const c : full) out.push_back(static_cast<std::byte>(c));
    return out;
}

// What toAdmin saw: each call's MsgType(35) and Text(58), in order. With throw_on_logout set,
// toAdmin throws on a Logout after recording it.
class TooLowApp104 final : public rg::GapApp {
public:
    struct Call {
        std::string msg_type;
        std::string text;
    };
    std::vector<Call> calls;
    bool throw_on_logout = false;

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
                 const fixpp::session::SessionId& id) override {
        rg::GapApp::toAdmin(msg, id);
        auto const t = msg.get(58);
        calls.push_back(Call{.msg_type = std::string{msg.msg_type()},
                             .text = t ? std::string{t->as_string()} : std::string{}});
        if (throw_on_logout && msg.msg_type() == "5") {
            throw std::runtime_error("TooLowApp104: toAdmin");
        }
    }
    [[nodiscard]] std::vector<Call> logouts() const {
        std::vector<Call> out;
        for (auto const& c : calls) {
            if (c.msg_type == "5") out.push_back(c);
        }
        return out;
    }
};

// A HookedStore whose factory reports a store that is not persistent, so the session neither
// hydrates from it nor ends on its store failures (024 I-07, logged then proceed).
class VolatileHookedFactory104 final : public fixpp::session::MessageStoreFactory {
public:
    explicit VolatileHookedFactory104(std::shared_ptr<HookedStoreFactory> hooked)
        : hooked_{std::move(hooked)} {}
    [[nodiscard]] bool yields_persistent_store() const noexcept override { return false; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        ++hooked_->log->stores_made;
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{
            std::make_unique<HookedStore>(hooked_->outbound_next, std::move(hooked_->hooks),
                                          hooked_->log, hooked_->mode)};
    }

private:
    std::shared_ptr<HookedStoreFactory> hooked_;
};

std::shared_ptr<fixpp::session::MemoryStore> seeded_memory_store() {
    auto st = std::make_shared<fixpp::session::MemoryStore>(
        fixpp::session::MemoryStore::Config{.policy = fixpp::session::capacity_policy::unbounded});
    asio::io_context seed;
    asio::co_spawn(
        seed,
        [st]() -> asio::awaitable<void> {
            for (seqnum_t i = 1; i < kN; ++i) {
                (void)co_await st->next_seqnum(direction_t::inbound, true);
            }
            for (seqnum_t i = 1; i < kOut; ++i) {
                (void)co_await st->next_seqnum(direction_t::outbound, true);
            }
        },
        asio::detached);
    seed.run();
    return st;
}

struct RigOpts {
    session_role role = session_role::initiator;
    // False: the HookedStore is wrapped in VolatileHookedFactory104.
    bool persistent = true;
    reset_seqnum_policy policy = reset_seqnum_policy::bilateral_lenient;
    bool reset_on_logout = false;
    bool mtls = false;
    std::string sender = "ISLD";
    std::string target = "TW";
    bool throw_to_admin_on_logout = false;
    fixpp::session::CompIdAuthorizationPolicy::logon_validator_fn validator = nullptr;
};

// A session on a HookedStore over a MemoryStore seeded at (kN, kOut). Its next armed outbound
// store() completes and then returns store_io_failure (the HookedStore's fail_held, with a
// release that holds nothing); its config-time sink can throw on a Logout.
class Rig {
public:
    struct Control {
        bool fail_next_store = false;
        bool throw_on_logout_write = false;
    };
    Control ctl;
    std::shared_ptr<HookedStoreFactory> factory = std::make_shared<HookedStoreFactory>();
    std::shared_ptr<TooLowApp104> app = std::make_shared<TooLowApp104>();
    rg::Scene s;
    RigOpts opts;

    explicit Rig(RigOpts o) : s{rg::Options{.role = o.role, .app = app}}, opts{std::move(o)} {
        factory->log->inner = seeded_memory_store();
        factory->hooks.on_outbound_store = [c = &ctl] {
            if (!c->fail_next_store) return false;
            c->fail_next_store = false;
            return true;
        };
        factory->hooks.release_when = [] { return true; };
        factory->hooks.fail_held = true;
        factory->log->call_snapshot = [this] {
            return fixpp::test_support::CallReading{.state = s.session->state()};
        };
        app->throw_on_logout = opts.throw_to_admin_on_logout;
        s.cfg.sender_comp_id = opts.sender;
        s.cfg.target_comp_id = opts.target;
        s.cfg.reset_seqnum_policy_field = opts.policy;
        s.cfg.reset_on_logout = opts.reset_on_logout;
        s.cfg.reset_on_disconnect = false;
        if (opts.persistent) {
            s.cfg.store_factory = factory;
        } else {
            s.cfg.store_factory = std::make_shared<VolatileHookedFactory104>(factory);
        }
        if (opts.mtls) {
            s.cfg.security_profile =
                fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
            s.cfg.compid_authorization_policy.add_binding("CN=TW-PROD-01,O=Peer,C=US", opts.target);
        }
        if (opts.validator) {
            s.cfg.compid_authorization_policy.set_logon_validator(opts.validator);
        }
        s.cfg.transport_send = [c = &ctl,
                                sink = s.cfg.transport_send](std::span<const std::byte> f) {
            std::string_view const v{reinterpret_cast<char const*>(f.data()), f.size()};
            if (c->throw_on_logout_write && rg::field(v, 35) == "5") {
                throw std::runtime_error("Rig: the sink's write of a Logout failed");
            }
            sink(f);
        };
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    }
    Rig(Rig const&) = delete;
    Rig& operator=(Rig const&) = delete;

    [[nodiscard]] std::string now_52(std::chrono::seconds offset = {}) const {
        return s.sending_time(offset);
    }
    // The peer's Logon (an acceptor's first frame, or an initiator's acknowledgement), with no
    // 141=Y, then `extra`.
    [[nodiscard]] std::vector<std::byte> logon(seqnum_t seq, std::string_view extra = {},
                                               std::chrono::seconds offset = {}) const {
        return peer_frame(opts.target, opts.sender, "A", seq, now_52(offset),
                          "98=0\x01"
                          "108=" +
                              std::to_string(rg::kHeartBtInt.count()) + "\x01" +
                              std::string{extra});
    }
    // The same Logon with PossDupFlag(43)=Y and OrigSendingTime(122) at SendingTime.
    [[nodiscard]] std::vector<std::byte> poss_dup_logon(seqnum_t seq) const {
        return logon(seq,
                     "43=Y\x01"
                     "122=" +
                         now_52() + "\x01");
    }
    [[nodiscard]] std::vector<std::byte> peer(std::string_view msg_type, seqnum_t seq) const {
        return peer_frame(opts.target, opts.sender, msg_type, seq, now_52(), {});
    }

    // open(): an initiator is then in LogonSent, having written its Logon; an acceptor stays in
    // NotConnected.
    [[nodiscard]] bool open() {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->open(), asio::use_future);
        if (!s.pump_until([&] { return rg::Scene::ready(fut); })) return false;
        if (!fut.get().has_value()) return false;
        return s.state() == (opts.role == session_role::initiator ? fsm_state::LogonSent
                                                                  : fsm_state::NotConnected);
    }

    [[nodiscard]] seqnum_t next_in() const {
        return session_test_access::seqnum_mgr(*s.session).next_inbound_unsafe();
    }
    [[nodiscard]] seqnum_t next_out() const {
        return session_test_access::seqnum_mgr(*s.session).next_outbound_unsafe();
    }
    void set_counters(seqnum_t in, seqnum_t out) {
        fixpp::session::seqnum_manager_test_access::set_counters(
            session_test_access::seqnum_mgr(*s.session), in, out);
    }
    // The store's next inbound number, read through next_seqnum(inbound, false), pumped. Empty
    // when the read fails or does not return.
    [[nodiscard]] std::optional<seqnum_t> store_next_in() {
        auto const st = factory->log->inner;
        auto const read = std::make_shared<std::optional<seqnum_t>>();
        auto const done = std::make_shared<bool>(false);
        asio::co_spawn(
            s.ioc,
            [st, read, done]() -> asio::awaitable<void> {
                auto in = co_await st->next_seqnum(direction_t::inbound, false);
                if (in) *read = *in;
                *done = true;
            },
            asio::detached);
        if (!s.pump_until([&] { return *done; })) return std::nullopt;
        return *read;
    }
    // The outbound frame the store holds at `seq`, or empty.
    [[nodiscard]] std::optional<std::string> stored_outbound(seqnum_t seq) {
        class Collect final : public fixpp::session::retrieve_visitor {
        public:
            std::optional<std::string> frame;
            asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
                seqnum_t, std::span<const std::byte> f) noexcept override {
                frame = std::string{reinterpret_cast<char const*>(f.data()), f.size()};
                co_return fixpp::session::visit_result::stop;
            }
        };
        auto const st = factory->log->inner;
        auto const visitor = std::make_shared<Collect>();
        auto const done = std::make_shared<bool>(false);
        asio::co_spawn(
            s.ioc,
            [st, visitor, done, seq]() -> asio::awaitable<void> {
                (void)co_await st->retrieve(seq, seq, direction_t::outbound, *visitor);
                *done = true;
            },
            asio::detached);
        if (!s.pump_until([&] { return *done; })) return std::nullopt;
        return visitor->frame;
    }
    [[nodiscard]] StoreLog const& log() const { return *factory->log; }
};

bool compid_authorization_failed(fixpp::session::Session const& sess) {
    for (auto const& ev : sess.recent_events()) {
        if (std::holds_alternative<fixpp::session::session_event_compid_authorization_failed>(ev)) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] std::string result_name(fixpp::core::expected_t<void> const& r) {
    return r.has_value() ? std::string{"success"} : std::to_string(static_cast<int>(r.error()));
}

// The next outbound number the arm's Logout takes: after the initiator's own Logon, or the
// hydrated one on the acceptor.
[[nodiscard]] seqnum_t expected_logout_seq(session_role r) {
    return r == session_role::initiator ? kOut + 1 : kOut;
}

// Opens the rig and checks the reach pins the arms share: the arm's state and, on the initiator,
// which hydrates at open(), NextNumIn kN, the next outbound number the Logout will take, and
// fixpp's own Logon without 141. The acceptor hydrates at its first Logon, so a cell reads its
// NextNumIn after the frame.
[[nodiscard]] ::testing::AssertionResult open_hydrated(Rig& r) {
    if (!r.open()) return ::testing::AssertionFailure() << "open() did not reach the arm's state";
    if (r.opts.role != session_role::initiator) return ::testing::AssertionSuccess();
    if (r.next_in() != kN) {
        return ::testing::AssertionFailure() << "NextNumIn " << r.next_in() << ", not " << kN;
    }
    if (r.next_out() != expected_logout_seq(r.opts.role)) {
        return ::testing::AssertionFailure() << "next outbound " << r.next_out();
    }
    auto const logons = r.s.of_type("A");
    if (logons.size() != 1U) return ::testing::AssertionFailure() << "fixpp's own Logon";
    if (!rg::field(logons.front(), 141).empty()) {
        return ::testing::AssertionFailure() << "fixpp's own Logon carried 141";
    }
    return ::testing::AssertionSuccess();
}

// T-1 to T-3's assertions on what a too-low frame at kM drew, from `mark`.
void expect_too_low_logout(Rig& r, std::size_t mark, fixpp::core::expected_t<void> const& res) {
    auto& s = r.s;
    EXPECT_TRUE(res.has_value()) << "the arm returned " << result_name(res);
    auto const logouts = s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "35=5 frames written: " << s.written(mark);
    auto const& lo = logouts.front();
    EXPECT_EQ(rg::field(lo, 58), too_low_text(kN, kM)) << "the Logout's Text(58)";
    EXPECT_TRUE(rg::field(lo, 1409).empty()) << "a SessionStatus(1409): " << rg::field(lo, 1409);
    EXPECT_TRUE(rg::field(lo, 789).empty())
        << "a NextExpectedMsgSeqNum(789): " << rg::field(lo, 789);
    EXPECT_EQ(rg::field(lo, 34), std::to_string(expected_logout_seq(r.opts.role)))
        << "the Logout's MsgSeqNum";
    EXPECT_EQ(r.next_in(), kN) << "NextNumIn in the manager";
    auto const st_in = r.store_next_in();
    ASSERT_TRUE(st_in.has_value()) << "the store's NextNumIn was not read";
    EXPECT_EQ(*st_in, kN) << "NextNumIn in the store";
    EXPECT_EQ(r.app->logouts().size(), 1U) << "toAdmin calls for a 35=5";
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
}

void logout_with_text(session_role const role, bool const poss_dup) {
    SCOPED_TRACE(role_name(role));
    Rig r{RigOpts{.role = role}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    auto const mark = r.s.sent.size();
    auto const res = r.s.feed(poss_dup ? r.poss_dup_logon(kM) : r.logon(kM));
    expect_too_low_logout(r, mark, res);
}

}  // namespace

// ── T078: T-1, T-2 and T-3 (contract T1, T2; research D6) ──────────────────────────────────

TEST(LogonTooLow104, Initiator_LogoutWithText) { logout_with_text(session_role::initiator, false); }

TEST(LogonTooLow104, Acceptor_LogoutWithText) { logout_with_text(session_role::acceptor, false); }

// T-3: with PossDupFlag(43)=Y, the same Logout, in both arms (research D6).
TEST(LogonTooLow104, PossDup_LogoutWithText) {
    logout_with_text(session_role::initiator, true);
    logout_with_text(session_role::acceptor, true);
}

// ── T079: T-4a to T-4f (contract T3 to T6; research D5's table) ────────────────────────────

namespace {

// T-4a: the longest CompID length L for which the arm's Logout fits kLogoutBufferBytes, with both
// CompIDs of length L, the session's SendingTime precision and the Logout's number. It is
// searched with the public build_logout against a buffer of that size.
[[nodiscard]] std::size_t near_bound_length(rg::Scene const& s, seqnum_t lo_seq) {
    std::array<char, 32> tbuf{};
    auto const t = fixpp::core::utc_time_to_fix_string(s.mock->now(), s.cfg.sending_time_precision,
                                                       std::span<char>{tbuf});
    if (!t) return 0;
    std::string const sending_time{t->data(), t->size()};
    std::string const text = too_low_text(kN, kM);
    std::array<std::byte, kLogoutBufferBytes> buf{};
    std::size_t last = 0;
    for (std::size_t len = 1; len < kLogoutBufferBytes; ++len) {
        auto const fits =
            fixpp::session::build_logout(std::span<std::byte>{buf}, lo_seq, std::string(len, 'S'),
                                         std::string(len, 'T'), text, "FIX.4.2", sending_time);
        if (!fits) break;
        last = len;
    }
    return last;
}

void overflow_arm(session_role const role, bool const over) {
    std::size_t len = 0;
    {
        rg::Scene probe{rg::Options{.role = role}};
        len = near_bound_length(probe, expected_logout_seq(role));
    }
    ASSERT_GT(len, 0U) << "setup: no CompID length fits";
    if (over) ++len;
    SCOPED_TRACE(std::string{role_name(role)} +
                 (over ? ", CompIDs of length L + 1" : ", CompIDs of length L") + " (" +
                 std::to_string(len) + ")");
    Rig r{RigOpts{.role = role, .sender = std::string(len, 'S'), .target = std::string(len, 'T')}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    if (role == session_role::initiator) {
        // fixpp's own Logon, in a buffer of the same size, carries the same CompIDs and a
        // SendingTime of the same precision.
        auto const logons = r.s.of_type("A");
        ASSERT_EQ(rg::field(logons.front(), 49), r.opts.sender) << "setup: the own Logon's 49";
    }
    auto const mark = r.s.sent.size();
    auto const res = r.s.feed(r.logon(kM));
    // The acceptor hydrates after interpret_logon, so NextNumIn kN shows its CompIDs were accepted.
    ASSERT_EQ(r.next_in(), kN) << "setup: NextNumIn";
    EXPECT_TRUE(res.has_value()) << "the arm returned " << result_name(res);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    if (over) {
        EXPECT_EQ(r.s.sent.size(), mark) << "frames written: " << r.s.written(mark);
        EXPECT_TRUE(r.app->logouts().empty()) << "toAdmin was called for a 35=5";
        return;
    }
    auto const logouts = r.s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "35=5 frames written: " << r.s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58), too_low_text(kN, kM)) << "the Logout's Text(58)";
    EXPECT_LE(logouts.front().size(), kLogoutBufferBytes) << "the Logout's size";
}

}  // namespace

// T-4a: L is the forced hit; at L + 1 the Logout does not fit, nothing is sent, and the session
// still ends.
TEST(LogonTooLow104, Overflow_PairedWithNearBound) {
    for (auto const role : {session_role::initiator, session_role::acceptor}) {
        overflow_arm(role, false);
        overflow_arm(role, true);
    }
}

namespace {

void to_admin_throw_arm(session_role const role) {
    SCOPED_TRACE(role_name(role));
    Rig r{RigOpts{.role = role, .throw_to_admin_on_logout = true}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    auto const mark = r.s.sent.size();
    auto const res = r.s.feed(r.logon(kM));
    auto const lo = r.app->logouts();
    ASSERT_EQ(lo.size(), 1U) << "toAdmin calls for a 35=5";
    EXPECT_EQ(lo.front().text, too_low_text(kN, kM)) << "the Text(58) toAdmin saw";
    EXPECT_EQ(r.s.sent.size(), mark) << "frames written: " << r.s.written(mark);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    EXPECT_TRUE(!res.has_value() && res.error() == fixpp::core::error::app_callback_threw)
        << "the arm returned " << result_name(res);
}

// T-4c: the next outbound number at seqnum_max, through the test seam, on a store the session
// does not hydrate from, so the seam's numbers stand when the frame arrives.
void assign_failure_arm(session_role const role) {
    SCOPED_TRACE(role_name(role));
    Rig r{RigOpts{.role = role, .persistent = false}};
    ASSERT_TRUE(r.open()) << "setup: open()";
    r.set_counters(kN, fixpp::session::seqnum_max);
    auto const mark = r.s.sent.size();
    auto const res = r.s.feed(r.logon(kM));
    ASSERT_EQ(r.next_in(), kN) << "setup: NextNumIn moved, or the seam's numbers did not stand";
    EXPECT_EQ(r.s.sent.size(), mark) << "frames written: " << r.s.written(mark);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    EXPECT_TRUE(!res.has_value() && res.error() == fixpp::core::error::store_seqnum_overflow)
        << "the arm returned " << result_name(res);
}

void persistent_store_failure_arm(session_role const role) {
    SCOPED_TRACE(role_name(role));
    Rig r{RigOpts{.role = role}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    r.ctl.fail_next_store = true;
    auto const mark = r.s.sent.size();
    auto const res = r.s.feed(r.logon(kM));
    EXPECT_FALSE(r.ctl.fail_next_store) << "setup: the armed store call was not made";
    auto const lo = r.app->logouts();
    ASSERT_EQ(lo.size(), 1U) << "toAdmin calls for a 35=5";
    EXPECT_EQ(lo.front().text, too_low_text(kN, kM)) << "the Text(58) toAdmin saw";
    EXPECT_EQ(r.s.sent.size(), mark) << "frames written: " << r.s.written(mark);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    EXPECT_TRUE(res.has_value()) << "the arm returned " << result_name(res);
}

void write_failure_arm(session_role const role) {
    SCOPED_TRACE(role_name(role));
    Rig r{RigOpts{.role = role}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    r.ctl.throw_on_logout_write = true;
    auto const out_before = expected_logout_seq(role);
    auto const mark = r.s.sent.size();
    auto const res = r.s.feed(r.logon(kM));
    auto const stored = r.stored_outbound(out_before);
    ASSERT_TRUE(stored.has_value()) << "the store holds no outbound frame at " << out_before;
    EXPECT_EQ(rg::field(*stored, 35), "5") << "the stored frame's MsgType";
    EXPECT_EQ(rg::field(*stored, 58), too_low_text(kN, kM)) << "the stored frame's Text(58)";
    EXPECT_EQ(r.s.sent.size(), mark) << "frames written: " << r.s.written(mark);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    EXPECT_TRUE(res.has_value()) << "the arm returned " << result_name(res);
}

// T-4f: a store the session does not hydrate from, so NextNumIn comes from the test seam.
void volatile_store_failure_arm(session_role const role) {
    SCOPED_TRACE(role_name(role));
    Rig r{RigOpts{.role = role, .persistent = false}};
    ASSERT_TRUE(r.open()) << "setup: open()";
    r.set_counters(kN, kOut);
    ASSERT_EQ(r.next_in(), kN) << "setup: NextNumIn";
    r.ctl.fail_next_store = true;
    auto const mark = r.s.sent.size();
    auto const res = r.s.feed(r.logon(kM));
    EXPECT_FALSE(r.ctl.fail_next_store) << "setup: the armed store call was not made";
    auto const logouts = r.s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "35=5 frames written: " << r.s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58), too_low_text(kN, kM)) << "the Logout's Text(58)";
    EXPECT_EQ(rg::field(logouts.front(), 34), std::to_string(kOut)) << "the Logout's MsgSeqNum";
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    EXPECT_TRUE(res.has_value()) << "the arm returned " << result_name(res);
}

}  // namespace

TEST(LogonTooLow104, ToAdminThrow) {
    to_admin_throw_arm(session_role::initiator);
    to_admin_throw_arm(session_role::acceptor);
}

TEST(LogonTooLow104, AssignFailure) {
    assign_failure_arm(session_role::initiator);
    assign_failure_arm(session_role::acceptor);
}

TEST(LogonTooLow104, PersistentStoreFailure) {
    persistent_store_failure_arm(session_role::initiator);
    persistent_store_failure_arm(session_role::acceptor);
}

TEST(LogonTooLow104, WriteFailure) {
    write_failure_arm(session_role::initiator);
    write_failure_arm(session_role::acceptor);
}

TEST(LogonTooLow104, VolatileStoreFailure) {
    volatile_store_failure_arm(session_role::initiator);
    volatile_store_failure_arm(session_role::acceptor);
}

// ── T080: T-5 (FR-015, contract T7) ────────────────────────────────────────────────────────

namespace {

// close(terminal), pumped by a bounded run. The session's mock clock does not move while it is
// pumped, so close()'s own bound (logon_timeout_ms on that clock) cannot end the wait inside the
// budget: a close() that waits reads as "not returned". It is then moved past logon_timeout_ms
// and pumped to completion, so the cell ends on its assertion.
[[nodiscard]] bool close_terminal_returned(rg::Scene& s) {
    auto fut =
        asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                       s.session->close(fixpp::session::close_mode::terminal), asio::use_future);
    if (s.pump_until([&] { return rg::Scene::ready(fut); }, fixpp::test_support::kHoldBound)) {
        (void)fut.get();
        return true;
    }
    s.mock->advance(std::chrono::milliseconds{s.cfg.logon_timeout_ms} +
                    std::chrono::milliseconds{1});
    (void)s.pump_until([&] { return rg::Scene::ready(fut); });
    return false;
}

}  // namespace

// T-5: an initiator with reset_on_logout. Its too-low Logout is not a receipt, so no reset
// follows, at the arm or at close(). The forced hit, on the same store record and fixture: a
// Logout received in LogonSent (arm S) records one reset_to.
TEST(LogonTooLow104, ResetOnLogout_NoResetForTooLowLogout) {
    {
        SCOPED_TRACE("too-low acknowledgement");
        Rig r{RigOpts{.reset_on_logout = true}};
        ASSERT_TRUE(open_hydrated(r)) << "setup";
        auto const mark = r.s.sent.size();
        (void)r.s.feed(r.logon(kM));
        auto const logouts = r.s.of_type("5", mark);
        ASSERT_EQ(logouts.size(), 1U) << "35=5 frames written: " << r.s.written(mark);
        EXPECT_EQ(rg::field(logouts.front(), 58), too_low_text(kN, kM)) << "the Logout's Text(58)";
        EXPECT_EQ(r.app->logouts().size(), 1U) << "toAdmin calls for a 35=5";
        EXPECT_TRUE(close_terminal_returned(r.s)) << "close(terminal) did not return";
        EXPECT_EQ(r.log().reset_to_calls, 0) << "reset_to calls";
        EXPECT_EQ(r.log().reset_calls, 0) << "reset() calls";
    }
    {
        SCOPED_TRACE("forced hit: a Logout received in LogonSent");
        Rig r{RigOpts{.reset_on_logout = true}};
        ASSERT_TRUE(open_hydrated(r)) << "setup";
        (void)r.s.feed(r.peer("5", kM));
        EXPECT_TRUE(close_terminal_returned(r.s)) << "close(terminal) did not return";
        EXPECT_EQ(r.log().reset_to_calls, 1) << "reset_to calls";
    }
}

// ── T081: T-6 (contract T8, "Precedence on one Logon frame"; FIX-TC 1S(c)) ─────────────────

namespace {

void sending_time_arm(session_role const role) {
    SCOPED_TRACE(role_name(role));
    Rig r{RigOpts{.role = role}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    auto const mark = r.s.sent.size();
    (void)r.s.feed(r.logon(kM, {}, std::chrono::seconds{-600}));
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    if (role == session_role::initiator) {
        auto const logouts = r.s.of_type("5", mark);
        ASSERT_EQ(logouts.size(), 1U) << "35=5 frames written: " << r.s.written(mark);
        EXPECT_EQ(rg::field(logouts.front(), 58), "SendingTime(52) accuracy")
            << "the Logout's Text(58)";
        return;
    }
    EXPECT_EQ(r.s.count("5", mark), 0U) << "35=5 frames written: " << r.s.written(mark);
    auto const rejects = r.s.of_type("3", mark);
    ASSERT_EQ(rejects.size(), 1U) << "35=3 frames written: " << r.s.written(mark);
    EXPECT_EQ(rg::field(rejects.front(), 371), "52") << "RefTagID";
    EXPECT_EQ(rg::field(rejects.front(), 373), "10") << "SessionRejectReason";
}

}  // namespace

// A too-low frame with a stale SendingTime takes the SendingTime guard's own ending, which runs
// before the sequence check in both arms.
TEST(LogonTooLow104, Precedence_SendingTime) {
    sending_time_arm(session_role::initiator);
    sending_time_arm(session_role::acceptor);
}

// A strict acceptor refuses a too-low Logon without 141=Y with its own Logout (103 D2.4).
TEST(LogonTooLow104, Precedence_StrictAcceptor) {
    Rig r{RigOpts{.role = session_role::acceptor, .policy = reset_seqnum_policy::bilateral_strict}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    auto const mark = r.s.sent.size();
    (void)r.s.feed(r.logon(kM));
    auto const logouts = r.s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "35=5 frames written: " << r.s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58), "ResetSeqNumFlag(141)=Y required")
        << "the Logout's Text(58)";
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
}

// An mTLS acceptor whose live identity is off the CompID's list: the gate's own ending, with
// nothing written on the attached transport or the config-time sink.
TEST(LogonTooLow104, Precedence_MtlsAcceptorOffList) {
    Rig r{RigOpts{.role = session_role::acceptor, .mtls = true}};
    auto const stream = std::make_shared<fixpp::session::test::ScriptedStream>();
    ASSERT_TRUE(r.open()) << "setup: open()";
    fixpp::transport::handshake_result hr{};
    hr.peer_id.subject_dn = "CN=OFF-LIST-104,O=Peer,C=US";
    r.s.session->attach_accepted_transport(
        std::make_unique<fixpp::session::test::ScriptedReadTransport>(stream), std::move(hr));
    auto const mark = r.s.sent.size();
    (void)r.s.feed(r.logon(kM));
    ASSERT_EQ(r.next_in(), kN) << "setup: NextNumIn was not hydrated to " << kN;
    EXPECT_TRUE(stream->written.empty())
        << "frames written on the attached transport: " << stream->written.size();
    EXPECT_EQ(r.s.sent.size(), mark) << "frames written on the sink: " << r.s.written(mark);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    EXPECT_TRUE(compid_authorization_failed(*r.s.session)) << "no authorisation-failed event";
}

// An acceptor whose credentials authorize_logon refuses: a counting refusing validator, called
// once, and nothing written.
TEST(LogonTooLow104, Precedence_RefusedCredentials) {
    auto const calls = std::make_shared<int>(0);
    Rig r{RigOpts{.role = session_role::acceptor,
                  .validator = [calls](std::string_view, fixpp::session::logon_credentials const&) {
                      ++*calls;
                      return false;
                  }}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    auto const mark = r.s.sent.size();
    (void)r.s.feed(r.logon(kM));
    EXPECT_EQ(*calls, 1) << "validator calls";
    EXPECT_EQ(r.s.sent.size(), mark) << "frames written: " << r.s.written(mark);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
}

// A direct mTLS initiator started by open() has no live identity, so the Logon path's gate fails
// closed in its arm (2): its event, and nothing written after fixpp's own Logon.
TEST(LogonTooLow104, Precedence_DirectMtlsInitiator) {
    Rig r{RigOpts{.mtls = true}};
    ASSERT_TRUE(open_hydrated(r)) << "setup";
    auto const mark = r.s.sent.size();
    (void)r.s.feed(r.logon(kM));
    EXPECT_EQ(r.s.sent.size(), mark) << "frames written: " << r.s.written(mark);
    EXPECT_EQ(r.s.state(), fsm_state::Disconnected) << rg::state_name(r.s.state());
    EXPECT_TRUE(compid_authorization_failed(*r.s.session)) << "no authorisation-failed event";
}
