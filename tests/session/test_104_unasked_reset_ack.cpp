// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_104_unasked_reset_ack.cpp — test-only.
//
// Feature 104 (specs/104-sequence-reset-fidelity/), fixpp#695: in LogonSent, an unasked 141=Y
// acknowledgement (fixpp's own Logon carried no 141=Y) is a reset before the sequence check, and
// a ResendRequest whose EndSeqNo is at or above the next outbound number is answered as one
// through the current number (research D3, D4, D3.3a; contract rows U1 to U7). The cells are
// research D10's U-1 to U-10 (tasks.md T063 to T067).
//
// They run on resend_gap_scene.hpp's Scene, whose first session is replaced before it opens, so
// the cell's session is built from the edited config. A cell-local builder gives the peer's
// frames the session's BeginString, because U-7's two arms differ in it.
//
// The persistent store is a HookedStore over a MemoryStore hydrated above 1 in both
// directions, to values distinct from each other and from every number a cell asserts after the
// acknowledgement, so a swapped or untouched counter cannot pass. Its per-call record
// (StoreLog::calls) holds each reset_to's targets. U-10's store is a StoreDouble, which does not
// override reset_to, so MessageStore's default body calls its reset(), which a cell can make
// fail.

#include <gtest/gtest.h>

#include <algorithm>
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
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_event.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <fixpp/tls/peer_identity.hpp>
#include <fixpp/tls/security_profile.hpp>
#include <fixpp/transport/endpoint.hpp>
#include <fixpp/transport/tls_transport.hpp>
#include <fixpp/transport/transport.hpp>
#include <fixpp/transport/transport_factory.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "session/resend_gap_scene.hpp"
#include "session/test_104_support.hpp"
#include "support/hooked_store.hpp"
#include "support/memory_store_test_access.hpp"
#include "support/session_executor_of.hpp"
#include "support/session_test_access.hpp"
#include "support/store_double.hpp"

namespace {

namespace rg = fixpp::test_support::resend_gap;
namespace plain_rig = fixpp::test_support::plain_rig;
namespace t104 = fixpp::test_support::t104;
using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::reset_seqnum_policy;
using fixpp::session::seqnum_t;
using fixpp::session::session_role;
using fixpp::session::session_test_access;
using fixpp::test_support::HookedStoreFactory;
using fixpp::test_support::StoreLog;

// The hydrated numbers: NextNumIn and the next outbound number the store holds before open().
constexpr seqnum_t kHydratedIn = 7;
constexpr seqnum_t kHydratedOut = 4;

using Numbers = t104::Numbers104;

std::shared_ptr<fixpp::session::MemoryStore> hydrated_memory_store() {
    auto st = std::make_shared<fixpp::session::MemoryStore>(
        fixpp::session::MemoryStore::Config{.policy = fixpp::session::capacity_policy::unbounded});
    fixpp::session::memory_store_test_access::set_counter(*st, direction_t::inbound, kHydratedIn);
    fixpp::session::memory_store_test_access::set_counter(*st, direction_t::outbound, kHydratedOut);
    return st;
}

// U-9's authorised twin: a TLS transport whose handshake reports a peer identity with a known
// CN, and its factory (test_live_identity_binding.cpp's IdentityInjectingTlsTransport, copied).
class IdentityTransport104 final : public fixpp::transport::TlsTransport {
public:
    explicit IdentityTransport104(std::string cn) : cn_{std::move(cn)} {}
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::ConnectInfo>>
    async_connect(fixpp::transport::Endpoint const& ep) override {
        fixpp::transport::ConnectInfo info;
        info.remote = ep;
        info.local = fixpp::transport::Endpoint{"127.0.0.1", 0};
        info.family = 2;
        co_return info;
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_read_some(
        std::span<std::byte> buf [[clang::lifetimebound]]) override {
        (void)buf;
        co_return std::unexpected{fixpp::core::error::transport_read_eof};
    }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<std::size_t>> async_write(
        std::span<const std::byte> buf [[clang::lifetimebound]]) override {
        co_return buf.size();
    }
    [[nodiscard]] fixpp::core::expected_t<void> cancel() noexcept override { return {}; }
    [[nodiscard]] fixpp::core::expected_t<void> close() noexcept override { return {}; }
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::transport::handshake_result>>
    async_handshake(fixpp::tls::SslCtxConfig const& cfg [[clang::lifetimebound]]) override {
        std::pmr::memory_resource* mr = cfg.mr ? cfg.mr : std::pmr::get_default_resource();
        fixpp::tls::peer_identity pid;
        pid.subject_dn = std::pmr::string{"CN=" + cn_, mr};
        pid.leaf_fingerprint = {};
        co_return fixpp::transport::handshake_result{
            .peer_id = std::move(pid),
            .captured_pinset = nullptr,
            .negotiated_cipher = std::pmr::string{"TLS_AES_128_GCM_SHA256", mr},
        };
    }

private:
    std::string cn_;
};
class IdentityFactory104 final : public fixpp::transport::TransportFactory {
public:
    explicit IdentityFactory104(std::string cn) : cn_{std::move(cn)} {}
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::transport::Transport>> make(
        asio::any_io_executor /*exec*/, fixpp::tls::SslCtxConfig /*ssl_cfg*/,
        std::pmr::memory_resource* /*mr*/) noexcept override {
        return std::make_unique<IdentityTransport104>(cn_);
    }
    [[nodiscard]] fixpp::core::expected_t<void> reload_credentials(
        std::shared_ptr<fixpp::tls::cert_source> /*s*/) noexcept override {
        return {};
    }
    [[nodiscard]] std::shared_ptr<fixpp::tls::cert_source> cert_source_snapshot()
        const noexcept override {
        return nullptr;
    }

private:
    std::string cn_;
};

// U-10's store: a StoreDouble shared with the cell, which does not override reset_to. Its
// factory keeps MessageStoreFactory's yields_persistent_store(), which is true.
class SharedStoreDouble104U final : public fixpp::session::MessageStore {
public:
    explicit SharedStoreDouble104U(std::shared_ptr<fixpp::session::test::StoreDouble> d) noexcept
        : fixpp::session::MessageStore(flush_thunk_for<SharedStoreDouble104U>()),
          d_{std::move(d)} {}
    asio::awaitable<fixpp::core::expected_t<void>> store(seqnum_t seq,
                                                         std::span<const std::byte> frame,
                                                         direction_t dir) noexcept override {
        return d_->store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        fixpp::session::retrieve_visitor& visitor) noexcept override {
        return d_->retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        return d_->next_seqnum(dir, increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        return (*d_).reset();
    }

private:
    std::shared_ptr<fixpp::session::test::StoreDouble> d_;
};
class StoreDoubleFactory104U final : public fixpp::session::MessageStoreFactory {
public:
    std::shared_ptr<fixpp::session::test::StoreDouble> store =
        std::make_shared<fixpp::session::test::StoreDouble>();
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<SharedStoreDouble104U>(store);
    }
};

enum class store_kind : std::uint8_t { hooked, store_double };

struct RigOpts {
    reset_seqnum_policy policy = reset_seqnum_policy::bilateral_lenient;
    std::string begin_string = "FIX.4.2";
    bool reset_on_logon = false;
    bool enable_789 = false;
    bool mtls = false;
    store_kind store = store_kind::hooked;
    // When set, the session's TransportFactory, so drive_reconnect() installs its transport.
    std::shared_ptr<fixpp::transport::TransportFactory> transport_factory = nullptr;
};

// An initiator on a store hydrated at (kHydratedIn, kHydratedOut).
class Rig {
public:
    std::shared_ptr<HookedStoreFactory> factory = std::make_shared<HookedStoreFactory>();
    std::shared_ptr<StoreDoubleFactory104U> double_factory =
        std::make_shared<StoreDoubleFactory104U>();
    std::vector<std::byte> payload;  // the application frame of send_order()
    rg::Scene s;
    std::string begin;

    explicit Rig(RigOpts const& o)
        : s{rg::Options{.role = session_role::initiator}}, begin{o.begin_string} {
        factory->log->inner = hydrated_memory_store();
        double_factory->store->seed_inbound(kHydratedIn);
        double_factory->store->seed_outbound(kHydratedOut);
        s.cfg.begin_string = o.begin_string;
        s.cfg.reset_seqnum_policy_field = o.policy;
        s.cfg.reset_on_logon = o.reset_on_logon;
        s.cfg.enable_next_expected_msg_seq_num = o.enable_789;
        if (o.store == store_kind::hooked) {
            s.cfg.store_factory = factory;
        } else {
            s.cfg.store_factory = double_factory;
        }
        if (o.mtls) {
            s.cfg.security_profile =
                fixpp::session::SecurityProfile{fixpp::session::SecurityProfile::kind::mtls_ca};
            s.cfg.compid_authorization_policy.add_binding("TW-PROD-01", "TW");
        }
        if (o.transport_factory) {
            s.cfg.transport_factory_override = o.transport_factory;
            s.cfg.reconnect_endpoint = fixpp::transport::Endpoint{"127.0.0.1", 19104};
        }
        s.session = std::make_unique<fixpp::session::Session>(s.engine, s.cfg);
    }

    [[nodiscard]] std::vector<std::byte> peer(std::string_view msg_type, seqnum_t seq,
                                              std::string_view extra = {}) const {
        return plain_rig::to_bytes(
            plain_rig::message(begin, msg_type, seq, "TW", "ISLD", s.sending_time(), extra));
    }
    // The peer's Logon acknowledgement with ResetSeqNumFlag(141)=Y, then `extra`.
    [[nodiscard]] std::vector<std::byte> ack(seqnum_t seq, std::string_view extra = {}) const {
        return peer("A", seq,
                    "98=0\x01"
                    "108=" +
                        std::to_string(s.cfg.heartbeat_interval.value_or(rg::kHeartBtInt).count()) +
                        "\x01"
                        "141=Y\x01" +
                        std::string{extra});
    }

    [[nodiscard]] bool open_to_logon_sent() {
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->open(), asio::use_future);
        if (!s.pump_until([&] { return rg::Scene::ready(fut); })) return false;
        return fut.get().has_value() && s.state() == fsm_state::LogonSent;
    }

    [[nodiscard]] Numbers manager() const { return t104::manager_numbers(*s.session); }
    // The store's next numbers: the StoreDouble's directly, the HookedStore's MemoryStore
    // through next_seqnum(dir, false), pumped. Empty when a read fails or does not return.
    [[nodiscard]] std::optional<Numbers> store_numbers(store_kind const k) {
        if (k == store_kind::store_double) {
            return Numbers{.in = double_factory->store->current_next_inbound(),
                           .out = double_factory->store->current_next_outbound()};
        }
        return t104::read_store_numbers(factory->log->inner.get(), s);
    }
    [[nodiscard]] StoreLog const& log() const { return *factory->log; }

    // Sends one application message and returns its result, or empty when it did not return.
    [[nodiscard]] std::optional<fixpp::core::expected_t<void>> send_order() {
        std::string const p =
            "35=D\x01"
            "11=ORD-104-U\x01" +
            std::string{rg::kSideAndTime};
        payload.assign(reinterpret_cast<std::byte const*>(p.data()),
                       reinterpret_cast<std::byte const*>(p.data()) + p.size());
        auto fut =
            asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                           s.session->send(std::span<const std::byte>{payload}), asio::use_future);
        if (!s.pump_until([&] { return rg::Scene::ready(fut); })) return std::nullopt;
        return fut.get();
    }
};

// The by_peer_request of each sequence_numbers_reset event, in order.
std::vector<bool> reset_events(fixpp::session::Session const& sess) {
    std::vector<bool> out;
    for (auto const& ev : sess.recent_events()) {
        if (auto const* r =
                std::get_if<fixpp::session::session_event_sequence_numbers_reset>(&ev)) {
            out.push_back(r->by_peer_request);
        }
    }
    return out;
}

bool visited(fixpp::session::Session const& sess, fsm_state const st) {
    return std::ranges::any_of(sess.fsm_visit_history(), [st](auto const v) { return v == st; });
}

// The reset_to calls on the record, as (in, out), and whether any reset() was recorded.
std::vector<Numbers> reset_tos(StoreLog const& log) {
    std::vector<Numbers> out;
    for (auto const& c : log.calls) {
        if (c.kind == StoreLog::call_kind::reset_to) {
            out.push_back({.in = c.next_in, .out = c.next_out});
        }
    }
    return out;
}

// fixpp's first frame (its Logon) did not carry 141=Y: the acknowledgement is unasked.
void expect_own_logon_unasked(rg::Scene const& s) {
    auto const logons = s.of_type("A");
    ASSERT_FALSE(logons.empty()) << "setup: fixpp wrote no Logon";
    EXPECT_TRUE(rg::field(logons.front(), 141).empty())
        << "setup: fixpp's Logon carried 141=" << rg::field(logons.front(), 141);
}

// U-1 and U-2: an unasked acknowledgement at 34=1 on a store hydrated above 1.
void unasked_at_one_resets_and_active(reset_seqnum_policy const policy) {
    Rig r{RigOpts{.policy = policy}};
    auto& s = r.s;
    ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
    ASSERT_EQ(r.manager().in, kHydratedIn) << "setup: the session did not hydrate";
    expect_own_logon_unasked(s);
    auto const mark = s.sent.size();
    auto const res = s.feed(r.ack(1));

    EXPECT_TRUE(res.has_value()) << "the acknowledgement's on_inbound_frame did not succeed";
    EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
    EXPECT_EQ(r.manager(), (Numbers{.in = 2, .out = 1}))
        << "the manager: " << r.manager().in << "/" << r.manager().out;
    auto const st = r.store_numbers(store_kind::hooked);
    ASSERT_TRUE(st.has_value()) << "the store's numbers were not read";
    EXPECT_EQ(*st, (Numbers{.in = 2, .out = 1})) << "the store: " << st->in << "/" << st->out;
    auto const tos = reset_tos(r.log());
    EXPECT_EQ(tos, (std::vector<Numbers>{{.in = 2, .out = 1}}))
        << "reset_to calls: " << tos.size() << ", reset() calls: " << r.log().reset_calls;
    EXPECT_EQ(reset_events(*s.session), std::vector<bool>{true})
        << "sequence_numbers_reset events (by_peer_request)";
    EXPECT_EQ(s.count("A", mark), 0U) << "a 35=A after fixpp's first: " << s.written(mark);
    auto const send_mark = s.sent.size();
    auto const sr = r.send_order();
    EXPECT_TRUE(sr.has_value() && sr->has_value()) << "send() did not succeed";
    auto const orders = s.of_type("D", send_mark);
    ASSERT_EQ(orders.size(), 1U) << "35=D frames written: " << s.written(send_mark);
    EXPECT_EQ(rg::field(orders.front(), 34), "1") << "the next send's MsgSeqNum";
}

}  // namespace

// ── T063: U-1 and U-2 (contract U1) ─────────────────────────────────────────────────────

TEST(UnaskedResetAck104, Lenient_HydratedN_ResetsAndActive) {
    unasked_at_one_resets_and_active(reset_seqnum_policy::bilateral_lenient);
}

TEST(UnaskedResetAck104, Unilateral_HydratedN_ResetsAndActive) {
    unasked_at_one_resets_and_active(reset_seqnum_policy::unilateral);
}

// ── T064: U-3 (FR-010's pin) and U-4 (contract U2) ──────────────────────────────────────

// U-3: bilateral_strict with reset_on_logon, so fixpp's own Logon carries 141=Y and the
// acknowledgement is asked. At 34=1 it is consumed against NextNumIn 1; at 34 = M > 1 it is too
// high and the session ends, with no 789 in fixpp's frames.
TEST(UnaskedResetAck104, Strict_OwnReset_Unchanged) {
    constexpr seqnum_t kM = 3;
    for (seqnum_t const seq : {seqnum_t{1}, kM}) {
        SCOPED_TRACE("ack 34=" + std::to_string(seq));
        Rig r{RigOpts{.policy = reset_seqnum_policy::bilateral_strict, .reset_on_logon = true}};
        auto& s = r.s;
        ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
        auto const logons = s.of_type("A");
        ASSERT_FALSE(logons.empty()) << "setup: fixpp wrote no Logon";
        ASSERT_EQ(rg::field(logons.front(), 141), "Y")
            << "setup: fixpp's own Logon did not carry 141=Y";
        auto const mark = s.sent.size();
        (void)s.feed(r.ack(seq));
        if (seq == 1) {
            EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
            EXPECT_EQ(r.manager().in, 2U) << "NextNumIn";
            continue;
        }
        EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
        EXPECT_FALSE(visited(*s.session, fsm_state::Active)) << "the session was Active";
        EXPECT_EQ(s.written(mark).find("789="), std::string::npos)
            << "a 789 in fixpp's frames: " << s.written(mark);
    }
}

// U-4: an unasked acknowledgement at 34 = M > 1 on NextNumIn kHydratedIn is not consumed: the
// session is Active at (1, 1), and the peer's next frame, at M + 1, draws ResendRequest 7=1,
// 16=0.
TEST(UnaskedResetAck104, AtMAbove1_NotConsumed_ResendOnNextFrame) {
    constexpr seqnum_t kM = 5;
    static_assert(kM > 1 && kM < kHydratedIn);
    Rig r{RigOpts{}};
    auto& s = r.s;
    ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
    expect_own_logon_unasked(s);
    (void)s.feed(r.ack(kM));
    EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
    EXPECT_EQ(r.manager(), (Numbers{.in = 1, .out = 1}))
        << "the manager: " << r.manager().in << "/" << r.manager().out;
    auto const tos = reset_tos(r.log());
    EXPECT_EQ(tos, (std::vector<Numbers>{{.in = 1, .out = 1}})) << "reset_to calls: " << tos.size();
    auto const mark = s.sent.size();
    (void)s.feed(r.peer("0", kM + 1));
    auto const rrs = s.of_type("2", mark);
    ASSERT_EQ(rrs.size(), 1U) << "ResendRequests written: " << s.written(mark);
    EXPECT_EQ(rg::field(rrs.front(), 7), "1") << "BeginSeqNo";
    EXPECT_EQ(rg::field(rrs.front(), 16), "0") << "EndSeqNo";
}

// ── T065: U-5 (contract U3) and U-6 (contract U5) ───────────────────────────────────────

// U-5: with 789 enabled, an unasked acknowledgement at 34=1 carrying 789 = X > 1. After the
// reset fixpp's next outbound number is 1, so X is too high and fixpp ends with a Logout whose
// text is spelled out here.
TEST(UnaskedResetAck104, With789_TooHighAfterReset_Logout) {
    constexpr seqnum_t kX = 9;
    static_assert(kX != kHydratedIn && kX != kHydratedOut + 1 && kX > 1);
    Rig r{RigOpts{.enable_789 = true}};
    auto& s = r.s;
    ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
    expect_own_logon_unasked(s);
    auto const mark = s.sent.size();
    (void)s.feed(r.ack(1, "789=" + std::to_string(kX) + "\x01"));
    auto const logouts = s.of_type("5", mark);
    ASSERT_EQ(logouts.size(), 1U) << "35=5 frames written: " << s.written(mark);
    EXPECT_EQ(rg::field(logouts.front(), 58),
              "NextExpectedMsgSeqNum too high, expecting 1 but received 9")
        << "the Logout's text";
    EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
}

// U-6: after U-1's input, the peer's ResendRequest at 34=2 for 7=1, 16=0 is answered by one
// GapFill at 34=1 to 36=1. It needs U-1's Active session, which the base does not reach.
TEST(UnaskedResetAck104, FollowUpResendRequest_GapFillOneOne) {
    Rig r{RigOpts{}};
    auto& s = r.s;
    ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
    (void)s.feed(r.ack(1));
    ASSERT_EQ(s.state(), fsm_state::Active)
        << "setup: U-1's input did not leave the session Active: " << rg::state_name(s.state());
    auto const mark = s.sent.size();
    (void)s.feed(r.peer("2", 2,
                        "7=1\x01"
                        "16=0\x01"));
    ASSERT_EQ(s.sent.size(), mark + 1)
        << "frames written for the ResendRequest: " << s.written(mark);
    auto const& gf = s.sent.back();
    EXPECT_EQ(rg::field(gf, 35), "4") << "MsgType";
    EXPECT_EQ(rg::field(gf, 34), "1") << "MsgSeqNum";
    EXPECT_EQ(rg::field(gf, 43), "Y") << "PossDupFlag";
    EXPECT_FALSE(rg::field(gf, 52).empty()) << "SendingTime";
    EXPECT_EQ(rg::field(gf, 122), rg::field(gf, 52)) << "OrigSendingTime against SendingTime";
    EXPECT_EQ(rg::field(gf, 123), "Y") << "GapFillFlag";
    EXPECT_EQ(rg::field(gf, 36), "1") << "NewSeqNo";
    EXPECT_EQ(r.manager().in, 3U) << "NextNumIn";
    auto const send_mark = s.sent.size();
    auto const sr = r.send_order();
    EXPECT_TRUE(sr.has_value() && sr->has_value()) << "send() did not succeed";
    auto const orders = s.of_type("D", send_mark);
    ASSERT_EQ(orders.size(), 1U) << "35=D frames written: " << s.written(send_mark);
    EXPECT_EQ(rg::field(orders.front(), 34), "1") << "the next send's MsgSeqNum";
}

// ── T066: U-7 and U-8 (research D3.3a, contract U6) ─────────────────────────────────────

// U-7: after U-1's input, a ResendRequest whose EndSeqNo is above the next outbound number is
// answered as one through the current number: the GapFill's NewSeqNo is 1. Two arms: FIX.4.1's
// 999999, and a FIX.4.4 explicit end. It needs U-1's Active session, as U-6 does.
TEST(UnaskedResetAck104, FollowUpResendRequest_ExplicitEnd_Clamped) {
    struct Arm {
        char const* begin_string;
        char const* end;
    };
    for (Arm const arm : {Arm{.begin_string = "FIX.4.1", .end = "999999"},
                          Arm{.begin_string = "FIX.4.4", .end = "50"}}) {
        SCOPED_TRACE(std::string{arm.begin_string} + " 16=" + arm.end);
        Rig r{RigOpts{.begin_string = arm.begin_string}};
        auto& s = r.s;
        ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
        (void)s.feed(r.ack(1));
        ASSERT_EQ(s.state(), fsm_state::Active)
            << "setup: U-1's input did not leave the session Active: " << rg::state_name(s.state());
        auto const mark = s.sent.size();
        (void)s.feed(r.peer("2", 2,
                            "7=1\x01"
                            "16=" +
                                std::string{arm.end} + "\x01"));
        auto const gfs = s.of_type("4", mark);
        ASSERT_EQ(gfs.size(), 1U) << "GapFills written: " << s.written(mark);
        EXPECT_EQ(rg::field(gfs.front(), 34), "1") << "MsgSeqNum";
        EXPECT_EQ(rg::field(gfs.front(), 36), "1") << "NewSeqNo";
    }
}

// U-8: Active with no reset and the next outbound number at kNextOut. A ResendRequest whose
// EndSeqNo is above it is answered through the current number: the GapFill's NewSeqNo is
// kNextOut, also when BeginSeqNo is above kNextOut (QuickFIX C++'s frame, NewSeqNo below
// MsgSeqNum).
TEST(ResendAnswer104, ExplicitEndAboveNextOut_NoOvershoot) {
    constexpr seqnum_t kNextOut = 5;
    rg::Scene s{rg::Options{.with_store = true}};
    ASSERT_TRUE(s.open_to_active()) << "setup: the session did not reach Active";
    std::vector<std::vector<std::byte>> payloads;
    while (session_test_access::seqnum_mgr(*s.session).next_outbound_unsafe() < kNextOut) {
        std::string const p =
            "35=D\x01"
            "11=ORD-104-U8\x01" +
            std::string{rg::kSideAndTime};
        payloads.emplace_back(reinterpret_cast<std::byte const*>(p.data()),
                              reinterpret_cast<std::byte const*>(p.data()) + p.size());
        auto fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                  s.session->send(std::span<const std::byte>{payloads.back()}),
                                  asio::use_future);
        ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(fut); })) << "setup: send()";
        ASSERT_TRUE(fut.get().has_value()) << "setup: send() failed";
    }
    ASSERT_EQ(session_test_access::seqnum_mgr(*s.session).next_outbound_unsafe(), kNextOut)
        << "setup: the next outbound number";
    struct Arm {
        seqnum_t begin;
        seqnum_t end;
    };
    for (Arm const arm : {Arm{.begin = kNextOut, .end = 10}, Arm{.begin = 10, .end = 20}}) {
        SCOPED_TRACE("RR 7=" + std::to_string(arm.begin) + " 16=" + std::to_string(arm.end));
        auto const mark = s.sent.size();
        (void)s.feed(s.frame(
            "2", s.next_in(),
            "7=" + std::to_string(arm.begin) + "\x01" + "16=" + std::to_string(arm.end) + "\x01"));
        auto const gfs = s.of_type("4", mark);
        ASSERT_EQ(gfs.size(), 1U) << "GapFills written: " << s.written(mark);
        EXPECT_EQ(rg::field(gfs.front(), 34), std::to_string(arm.begin)) << "MsgSeqNum";
        EXPECT_EQ(rg::field(gfs.front(), 36), std::to_string(kNextOut)) << "NewSeqNo";
        EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
    }
}

// ── T067: U-9 (contract U4m, clarification N-8) and U-10 (contract U7) ──────────────────

// U-9: an mTLS initiator started by open() has no live identity, so the Logon path's mTLS gate
// fails closed on the unasked acknowledgement at 34=1, before any unit. The forced hit is the
// authorised twin: the same initiator, brought back to LogonSent by drive_reconnect() over a
// transport whose handshake reports an identity the policy binds to TW, so the gate calls
// authorize, passes, and the unit runs.
TEST(UnaskedResetAck104, Mtls_Unauthorised_NoReset) {
    {
        SCOPED_TRACE("direct open(), no live identity");
        Rig r{RigOpts{.mtls = true}};
        auto& s = r.s;
        ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
        expect_own_logon_unasked(s);
        auto const before = r.manager();
        ASSERT_EQ(before.in, kHydratedIn) << "setup: the session did not hydrate";
        (void)s.feed(r.ack(1));
        EXPECT_TRUE(r.log().calls.empty()) << "a reset call: reset() " << r.log().reset_calls
                                           << ", reset_to " << r.log().reset_to_calls;
        EXPECT_EQ(r.manager(), before)
            << "the manager: " << r.manager().in << "/" << r.manager().out;
        EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
        EXPECT_TRUE(t104::compid_authorization_failed(*s.session))
            << "no authorisation-failed event";
    }
    {
        SCOPED_TRACE("authorised twin, live identity through drive_reconnect()");
        Rig r{RigOpts{.mtls = true,
                      .transport_factory = std::make_shared<IdentityFactory104>("TW-PROD-01")}};
        auto& s = r.s;
        ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
        // A Logout in LogonSent ends the first connection; reset_on_logout is off, so it
        // makes no reset.
        (void)s.feed(r.peer("5", 2));
        ASSERT_EQ(s.state(), fsm_state::Disconnected) << "setup: the first connection did not end";
        auto rc_fut = asio::co_spawn(fixpp::test_support::session_executor_of(*s.session, s.ioc),
                                     s.session->drive_reconnect(), asio::use_future);
        ASSERT_TRUE(s.pump_until([&] { return rg::Scene::ready(rc_fut); }))
            << "setup: drive_reconnect() did not return";
        ASSERT_TRUE(rc_fut.get().has_value()) << "setup: drive_reconnect() failed";
        ASSERT_EQ(s.state(), fsm_state::LogonSent) << "setup: not in LogonSent after reconnect";
        ASSERT_TRUE(r.log().calls.empty()) << "setup: a reset call before the acknowledgement";
        ASSERT_GT(r.manager().in, 1U) << "setup: NextNumIn at 1, so the acknowledgement is asked";
        (void)s.feed(r.ack(1));
        auto const tos = reset_tos(r.log());
        EXPECT_EQ(tos, (std::vector<Numbers>{{.in = 2, .out = 1}}))
            << "reset_to calls: " << tos.size();
        EXPECT_FALSE(t104::compid_authorization_failed(*s.session))
            << "an authorisation-failed event";
    }
}

// U-10: a persistent store that refuses the unit's reset. 030's persistent-fatal ending: the
// store saw one reset() call (its reset_to is MessageStore's default body), the arm returns the
// store's error, the session ends without reaching Active, and the store keeps the numbers it
// held before the acknowledgement. The forced hit is the same store without the failure.
TEST(UnaskedResetAck104, PersistentStoreRefusesReset_Fatal) {
    for (bool const fail : {true, false}) {
        SCOPED_TRACE(fail ? "the reset fails" : "the reset succeeds");
        Rig r{RigOpts{.store = store_kind::store_double}};
        auto& s = r.s;
        ASSERT_TRUE(r.open_to_logon_sent()) << "setup: the session did not reach LogonSent";
        expect_own_logon_unasked(s);
        auto const before = r.store_numbers(store_kind::store_double);
        ASSERT_TRUE(before.has_value() && before->in == kHydratedIn)
            << "setup: the store was not hydrated";
        if (fail) r.double_factory->store->fail_next_reset();
        auto const mark = s.sent.size();
        auto const res = s.feed(r.ack(1));
        if (!fail) {
            EXPECT_EQ(s.state(), fsm_state::Active) << rg::state_name(s.state());
            continue;
        }
        EXPECT_EQ(r.double_factory->store->reset_call_count(), 1U) << "reset() calls";
        EXPECT_TRUE(!res.has_value() && res.error() == fixpp::core::error::store_io_failure)
            << "the arm's return: "
            << (res.has_value() ? std::string{"success"}
                                : std::to_string(static_cast<int>(res.error())));
        EXPECT_EQ(s.state(), fsm_state::Disconnected) << rg::state_name(s.state());
        EXPECT_FALSE(visited(*s.session, fsm_state::Active)) << "the session was Active";
        EXPECT_EQ(s.sent.size(), mark) << "frames after fixpp's Logon: " << s.written(mark);
        EXPECT_EQ(r.store_numbers(store_kind::store_double), before) << "the store's numbers";
    }
}
