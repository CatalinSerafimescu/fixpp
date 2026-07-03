// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_store_fail_reconcile_outofset.cpp
//
// gate-b/r1 FQ-1 (RC#1) — RED witness for the fatal-class predicate unification.
//
// test_store_fail_reconcile_breadth.cpp only exercises the three FileStore-
// reachable codes (store_io_failure, store_seqnum_out_of_order,
// store_capacity_exhausted). Pre-fix, Session::send's guard was a CLOSED
// 3-code set (session.cpp ~:4064) that disagreed with store_then_emit's own
// durability-classified gate (session.cpp ~:4804: any non-cancelled error on
// a persistent store). A custom persistent MessageStore whose store() returns
// a store-block code OUTSIDE that 3-set (e.g. store_seqnum_gap=57,
// store_seqnum_invalid=63 — see opus_pr163_1_triage.md RC#1) got the fatal
// return from store_then_emit (no transmit — correct) but Session::send did
// NOT transition to Disconnected — the session stayed Active.
//
// This is a fresh, standalone breadth-style test (authored separately from
// test_store_fail_reconcile_breadth.cpp per gate-b/r1 brief §FQ-1 — not
// folded into an existing fixture) parametrized over two OUT-OF-SET codes.
//
// RED (pre-fix): for EACH out-of-set code, Session::send returns ok (or an
// unrelated code) and the session stays Active — these assertions FAIL.
// GREEN (post-fix, is_persistent_retain_fatal predicate landed): Session::send
// returns the store's own error un-coerced, the session is Disconnected, and
// the frame is not transmitted.
//
// Anchors: opus_pr163_1_triage.md Fix queue FQ-1;
// specs/059-outbound-store-fail-closed/spec.md FR-004;
// contracts/store-then-emit-disposition.md item 3.
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

namespace {

using fixpp::session::direction_t;
using fixpp::session::fsm_state;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::Session;
using fixpp::session::session_role;
using fixpp::session::seqnum_t;

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

std::string extract_tag(const std::vector<std::byte>& frame, int tag) {
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

// ── ArmableFaultStore: a minimal PERSISTENT store double ─────────────────────
//
// Same shape as test_store_fail_reconcile_breadth.cpp's double — a fresh copy
// per gate-b/r1 brief §FQ-1 ("author a fresh breadth-style test... do NOT
// merely fold into an existing fixture"). yields_persistent_store() is NOT
// overridden by ArmableFaultStoreFactory — MessageStoreFactory::
// yields_persistent_store() defaults to true (research.md D6 fallback), so
// this store IS classified persistent.
class ArmableFaultStore final : public MessageStore {
public:
    ArmableFaultStore(fixpp::core::error err, seqnum_t fail_at_outbound_seq)
        : MessageStore(flush_thunk_for<ArmableFaultStore>()),
          err_(err),
          fail_at_(fail_at_outbound_seq) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> /*frame*/, direction_t dir) noexcept override {
        if (dir == direction_t::outbound && seq == fail_at_ && !fired_) {
            fired_ = true;
            co_return std::unexpected(err_);
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        auto& c = (dir == direction_t::outbound) ? next_out_ : next_in_;
        const seqnum_t cur = c;
        if (increment) ++c;
        co_return cur;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_in_ = next_out_ = 1;
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] bool fired() const noexcept { return fired_; }

private:
    fixpp::core::error err_;
    seqnum_t fail_at_;
    bool fired_ = false;
    seqnum_t next_in_ = 1;
    seqnum_t next_out_ = 1;
};

class ArmableFaultStoreFactory final : public MessageStoreFactory {
public:
    ArmableFaultStoreFactory(fixpp::core::error err, seqnum_t fail_at_outbound_seq)
        : err_(err), fail_at_(fail_at_outbound_seq) {}

    mutable ArmableFaultStore* last_store = nullptr;

    // NOTE: no yields_persistent_store() override — defaults to true (persistent).

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        auto s = std::make_unique<ArmableFaultStore>(err_, fail_at_);
        last_store = s.get();
        return s;
    }

private:
    fixpp::core::error err_;
    seqnum_t fail_at_;
};

// ── Fixture ───────────────────────────────────────────────────────────────────

class StoreFailReconcileOutOfSetTest : public ::testing::TestWithParam<fixpp::core::error> {
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

TEST_P(StoreFailReconcileOutOfSetTest, PersistentStore_FailsClosed_ForOutOfSetErrorCode) {
    const fixpp::core::error injected_err = GetParam();

    // Logon consumes seq=1; the first app-level send() is assigned seq=2.
    constexpr seqnum_t kFailAtSeq = 2;
    auto factory = std::make_shared<ArmableFaultStoreFactory>(injected_err, kFailAtSeq);

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
    ioc.run_for(200ms);
    ioc.restart();
    ASSERT_TRUE(open_r.get().has_value()) << "open() must succeed";
    ASSERT_EQ(sess.state(), fsm_state::LogonSent);

    auto peer_logon = make_logon("FIX.4.2", 1, "ACCEPTR", "INITR");
    auto logon_r = asio::co_spawn(ioc, sess.on_inbound_frame(std::span<const std::byte>(peer_logon)),
                                  asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    ASSERT_TRUE(logon_r.get().has_value()) << "peer Logon-ack must be accepted";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    auto payload = make_app_payload("ORD1");
    auto send_fut = asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    ioc.run_for(200ms);
    ioc.restart();
    auto send_r = send_fut.get();

    ASSERT_NE(factory->last_store, nullptr) << "the factory must have minted a store";
    ASSERT_TRUE(factory->last_store->fired())
        << "the fault-injection seam must have fired for out-of-set error class "
        << static_cast<int>(injected_err) << " (seq=" << kFailAtSeq << "); otherwise the "
        << "assertions below prove nothing (feedback_fail_placeholder_red_test)";

    // ── RED assertions (fail on the pre-fix 3-code-set guard; the class
    //    predicate lands them GREEN) — RC#1 / FQ-1. ──
    EXPECT_FALSE(send_r.has_value())
        << "RC#1 (currently RED pre-fix): a persistent-store retain failure of the "
           "OUT-OF-SET class " << static_cast<int>(injected_err)
        << " must fail closed exactly as the three enumerated classes do";
    if (!send_r.has_value()) {
        EXPECT_EQ(send_r.error(), injected_err)
            << "the store's own error code must propagate un-coerced (D2 / contract item 3)";
    }
    EXPECT_EQ(sess.state(), fsm_state::Disconnected)
        << "RC#1 (currently RED pre-fix): the session must transition to Disconnected for "
           "out-of-set error class " << static_cast<int>(injected_err);
    bool failing_frame_transmitted = false;
    for (const auto& frame : wire) {
        if (extract_tag(frame, 34) == std::to_string(kFailAtSeq)) failing_frame_transmitted = true;
    }
    EXPECT_FALSE(failing_frame_transmitted)
        << "the failing frame (seq=" << kFailAtSeq
        << ") must NOT be transmitted for out-of-set error class " << static_cast<int>(injected_err);
}

INSTANTIATE_TEST_SUITE_P(
    OutOfSetErrorClasses, StoreFailReconcileOutOfSetTest,
    ::testing::Values(fixpp::core::error::store_seqnum_gap,
                      fixpp::core::error::store_seqnum_invalid),
    [](const ::testing::TestParamInfo<fixpp::core::error>& info) -> std::string {
        switch (info.param) {
            case fixpp::core::error::store_seqnum_gap:
                return "store_seqnum_gap";
            case fixpp::core::error::store_seqnum_invalid:
                return "store_seqnum_invalid";
            default:
                return "unknown";
        }
    });

}  // namespace
