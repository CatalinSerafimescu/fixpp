// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_next_expected_msgseqnum.cpp
//
// 027-next-expected-msgseqnum unit test suite.
//
// Phase 2 (Foundational) witnesses — T004:
//   WalkExtraction_TwoValueEnd_ExplicitEndBeyondStore (ResendRequest-caller half):
//     request [10,20], store through 5 => GapFill NewSeqNo=21, NOT 6.
//     Guards the shipped 013 ResendRequest path regression (I-NEX-3).
//   WalkExtraction_TwoValueEnd_EndSeqNo0_EmptyStore (ResendRequest-caller half):
//     EndSeqNo=0 / through-current, empty store => GapFill NewSeqNo=peek_outbound().
//   WalkExtraction_SingleImplementation:
//     Structural grep: the store-walk body exists in exactly ONE place in
//     session.cpp after the T005 extraction.
//
// Anchors: research.md D-5, data-model.md I-NEX-3, contracts C3.
//
// US1-US3 witnesses (T007-T021) are added in later phases.
// Production-shape: drives bytes through Session::on_inbound_frame().

#include <gtest/gtest.h>
#include <sys/stat.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// ── Application stubs for FQ-1 / FQ-2 witnesses ──────────────────────────────

// CountingApp027: records onLogon + toAdmin invocation counts.
// Used to assert (a) onLogon was NOT called on integrity-error paths (FQ-1),
// and (b) toAdmin IS called for 789-triggered Logout frames (FQ-2).
class CountingApp027 : public fixpp::session::Application {
public:
    int onLogon_count{0};
    int toAdmin_count{0};

    void onLogon(const fixpp::session::SessionId& /*id*/) override { ++onLogon_count; }

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {
        ++toAdmin_count;
    }
};

// ThrowingToAdmin027: throws from toAdmin on the N-th call.
// Used to witness that a toAdmin throw on the 789-Logout path surfaces
// app_callback_threw + terminal-closes (FQ-2 negative case).
class ThrowingToAdmin027 : public fixpp::session::Application {
public:
    explicit ThrowingToAdmin027(int n) : throw_at_(n) {}
    mutable int call_count{0};
    const int throw_at_;

    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& /*msg*/,
                 const fixpp::session::SessionId& /*id*/) override {
        ++call_count;
        if (call_count == throw_at_) {
            throw std::runtime_error("toAdmin throw from 789 Logout path");
        }
    }
};

// ── Frame-building helpers (mirror test_recovery_*.cpp pattern) ───────────────

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

static std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// make_logon_with_789: Logon frame carrying NextExpectedMsgSeqNum(789).
// make_app_frame_posdup: app frame (35=D) with PossDupFlag(43)=Y + OrigSendingTime(122).
static std::vector<std::byte> make_app_frame_posdup(std::string_view bs, std::uint32_t seq,
                                                    std::string_view s, std::string_view t) {
    std::string extra;
    extra += field(43, "Y");
    extra += field(97, "N");  // PossResend=N
    extra += field(122, "20240101-00:00:00.000");
    extra += field(11, "ORD" + std::to_string(seq));
    return make_fix_frame(bs, "D", seq, s, t, extra);
}

static std::vector<std::byte> make_logon_with_789(std::string_view bs, std::uint32_t seq,
                                                  std::string_view s, std::string_view t,
                                                  std::uint32_t next_expected, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    extra += field(789, std::to_string(next_expected));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

static std::vector<std::byte> make_resend_request(std::string_view bs, std::uint32_t seq,
                                                  std::string_view s, std::string_view t,
                                                  std::uint32_t begin_seqno,
                                                  std::uint32_t end_seqno) {
    std::string extra;
    extra += field(7, std::to_string(begin_seqno));
    extra += field(16, std::to_string(end_seqno));
    return make_fix_frame(bs, "2", seq, s, t, extra);
}

// make_seq_reset_gapfill: SequenceReset-GapFill for admin fills during resend.
static std::vector<std::byte> make_seq_reset_gapfill(std::string_view bs, std::uint32_t seq,
                                                     std::string_view s, std::string_view t,
                                                     std::uint32_t new_seqno) {
    std::string extra;
    extra += field(43, "Y");
    extra += field(123, "Y");  // GapFillFlag=Y
    extra += field(36, std::to_string(new_seqno));
    return make_fix_frame(bs, "4", seq, s, t, extra);
}

// ── Outbound capture ──────────────────────────────────────────────────────────

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data) {
        frames.emplace_back(data.begin(), data.end());
    }
};

// ── Frame field extractors ────────────────────────────────────────────────────

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

static bool frame_is_gapfill(const std::vector<std::byte>& frame) {
    return extract_tag(frame, 35) == "4" && extract_tag(frame, 123) == "Y";
}

// ── MessageStore doubles ──────────────────────────────────────────────────────

using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

// ShortStore: outbound seqnums [1..last_stored] are app messages (35=D).
// Simulates a store populated only through seqnum last_stored.
// next_seqnum(outbound) = last_stored + 1.
class ShortStore final : public MessageStore {
public:
    explicit ShortStore(seqnum_t last_stored)
        : MessageStore(flush_thunk_for<ShortStore>()), next_outbound_(last_stored + 1U) {
        for (seqnum_t k = 1; k <= last_stored; ++k) {
            const auto raw = make_fix_frame("FIX.4.4", "D", k, "SRV", "CLI",
                                            field(11, "ORD" + std::to_string(k)));
            frames_[k] = raw;
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
        direction_t /*dir*/) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor) noexcept override {
        if (dir != direction_t::outbound) {
            co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
        }
        const seqnum_t hi = (end == 0) ? (next_outbound_ - 1U) : end;
        for (seqnum_t k = begin; k <= hi; ++k) {
            auto it = frames_.find(k);
            if (it == frames_.end()) {
                co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
            }
            auto r = co_await visitor.on_frame(
                k, std::span<const std::byte>{it->second.data(), it->second.size()});
            if (!r) co_return std::unexpected(r.error());
            if (*r != visit_result::cont) break;
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool /*increment*/) noexcept override {
        co_return dir == direction_t::outbound ? next_outbound_ : seqnum_t{1};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

private:
    seqnum_t next_outbound_;
    std::map<seqnum_t, std::vector<std::byte>> frames_;
};

class ShortStoreFactory final : public MessageStoreFactory {
public:
    explicit ShortStoreFactory(seqnum_t last_stored) : last_stored_(last_stored) {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::unique_ptr<MessageStore>(new ShortStore(last_stored_));
    }

private:
    seqnum_t last_stored_;
};

// EmptyStore: no stored frames; next_seqnum(outbound) = 1.
class EmptyStore final : public MessageStore {
public:
    EmptyStore() : MessageStore(flush_thunk_for<EmptyStore>()) {}

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

class EmptyStoreFactory final : public MessageStoreFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::unique_ptr<MessageStore>(new EmptyStore());
    }
};

// ── Session fixture ───────────────────────────────────────────────────────────

// Builds an acceptor Session through its Logon handshake (Active state).
// The peer Logon is sent at seq=1 by default. Returns the fixture; the
// outbound capture may contain the acceptor's reply Logon.

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

static std::unique_ptr<Fixture> make_acceptor(std::shared_ptr<MessageStoreFactory> store_factory,
                                              std::uint32_t peer_logon_seq = 1,
                                              bool enable_789 = false) {
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
    // bilateral_lenient: accept peer Logon without requiring matching 141=Y.
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = enable_789;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };

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

// ── Initiator fixture ─────────────────────────────────────────────────────────
//
// Builds an initiator Session and calls open() to emit the outbound Logon
// (which transitions the session to LogonSent). The outbound capture holds
// exactly the Logon frame after open().

static std::unique_ptr<Fixture> make_initiator(std::shared_ptr<MessageStoreFactory> store_factory,
                                               bool enable_789 = false) {
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
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };

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

// ── Phase 2 (Foundational) — T004 walk-extraction safety witnesses ────────────
//
// The two TwoValueEnd witnesses assert EXISTING 013 behavior (they pass before
// and after T005 extraction). The SingleImplementation witness is RED until
// T005 extracts replay_outbound_range_() (grep count is 1 both before and
// after extraction, because the loop moves 1:1 — but the second grep for
// the function name is RED before extraction).

// T004 witness 1 (ResendRequest-caller half, research D-5, I-NEX-3):
// Peer sends ResendRequest([10,20]) when our store only has [1..5].
// Response MUST be SequenceReset-GapFill with NewSeqNo=21 (rr_end+1), NOT 6
// (eff_end+1). This guards the two-value end model in the existing inline walk.
TEST(WalkExtraction, TwoValueEnd_ExplicitEndBeyondStore) {
    // Store has seqnums [1..5]; next outbound = 6.
    auto fix = make_acceptor(std::make_shared<ShortStoreFactory>(5));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);
    fix->clear_capture();

    // Peer sends ResendRequest(BeginSeqNo=10, EndSeqNo=20).
    fix->feed(make_resend_request("FIX.4.4", 2, "CLI", "SRV", 10, 20));

    const auto& frames = fix->capture.frames;
    auto gf_it = std::find_if(frames.begin(), frames.end(), frame_is_gapfill);
    ASSERT_NE(gf_it, frames.end()) << "Expected a SequenceReset-GapFill response";

    // NewSeqNo(36) MUST be rr_end+1 = 21, NOT eff_end+1 = 6.
    const std::string new_seqno = extract_tag(*gf_it, 36);
    EXPECT_EQ(new_seqno, "21")
        << "Two-value-end regression: GapFill NewSeqNo must be 21 (rr_end+1), not 6 (eff_end+1)";

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);
}

// T004 witness 2 (ResendRequest-caller half, research D-5, I-NEX-3):
// Peer sends ResendRequest(BeginSeqNo=1, EndSeqNo=0 = through-current) against
// an empty store. Response MUST be GapFill with NewSeqNo=peek_outbound().
//
// After the acceptor's reply Logon (seq=1), peek_outbound()==2. The empty store
// returns next_seqnum(outbound)=1 so our_last=0; the early-GapFill fires.
// NewSeqNo = (rr_end==0) ? peek_outbound() : (rr_end+1) = peek_outbound() = 2.
TEST(WalkExtraction, TwoValueEnd_EndSeqNo0_EmptyStore) {
    auto fix = make_acceptor(std::make_shared<EmptyStoreFactory>());
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    // After the Logon handshake: acceptor sent its reply Logon at seq=1.
    // The store is empty (next_seqnum=1) — the store doesn't record the
    // Logon (EmptyStore::store no-ops), so peek_outbound() = 2.
    const std::uint32_t expected_new_seqno = 2;  // peek_outbound() post-reply Logon

    fix->clear_capture();

    // Peer sends ResendRequest(BeginSeqNo=1, EndSeqNo=0 = through-current).
    fix->feed(make_resend_request("FIX.4.4", 2, "CLI", "SRV", 1, 0));

    const auto& frames = fix->capture.frames;
    auto gf_it = std::find_if(frames.begin(), frames.end(), frame_is_gapfill);
    ASSERT_NE(gf_it, frames.end()) << "Expected a SequenceReset-GapFill response";

    const std::string new_seqno = extract_tag(*gf_it, 36);
    EXPECT_EQ(new_seqno, std::to_string(expected_new_seqno))
        << "EndSeqNo=0 empty-store: GapFill NewSeqNo must be peek_outbound()="
        << expected_new_seqno;

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);
}

// T004 witness 3 — structural assertion (research D-5, I-NEX-3):
// After T005 extraction, replay_outbound_range_() is the sole store-walk
// implementation. The ResendRequest handler and (later) the 789 path both
// call it; no inline walk body remains in the handler.
//
// Mechanism: grep session.cpp for the characteristic store-walk loop token
// ("for (seqnum_t k = ") — must appear exactly ONCE (inside
// replay_outbound_range_()); also verify replay_outbound_range_ is defined.
//
// RED before T005: grep for "replay_outbound_range_" returns 0 matches.
// GREEN after T005: grep for "replay_outbound_range_" returns >= 1 match,
//                  loop token appears exactly once.
TEST(WalkExtraction, SingleImplementation) {
    const char* src_dir = nullptr;
#ifdef FIXPP_TEST_SOURCE_DIR
    src_dir = FIXPP_TEST_SOURCE_DIR;
#endif
    std::string session_cpp;
    if (src_dir && *src_dir) {
        session_cpp = std::string(src_dir) + "/src/session/session.cpp";
    } else {
        session_cpp = "../../src/session/session.cpp";
    }

    // grep -c returns the count of matching lines.
    const std::string loop_token = "for (seqnum_t k = ";
    const std::string grep_loop_cmd =
        "grep -c '" + loop_token + "' '" + session_cpp + "' 2>/dev/null";
    FILE* fp_loop = popen(grep_loop_cmd.c_str(), "r");
    ASSERT_NE(fp_loop, nullptr);
    int loop_count = 0;
    (void)fscanf(fp_loop, "%d", &loop_count);
    pclose(fp_loop);

    // After T005 extraction the loop is in exactly ONE place.
    EXPECT_EQ(loop_count, 1) << "Store-walk loop must exist exactly once in session.cpp "
                             << "(inside replay_outbound_range_() after T005). "
                             << "count=" << loop_count;

    // replay_outbound_range_ must be defined in session.cpp after T005.
    const std::string grep_fn_cmd =
        "grep -c 'replay_outbound_range_' '" + session_cpp + "' 2>/dev/null";
    FILE* fp_fn = popen(grep_fn_cmd.c_str(), "r");
    ASSERT_NE(fp_fn, nullptr);
    int fn_count = 0;
    (void)fscanf(fp_fn, "%d", &fn_count);
    pclose(fp_fn);

    EXPECT_GE(fn_count, 1)
        << "replay_outbound_range_ must appear in session.cpp after T005 extraction";
}

// ── T007 — US1 emit witnesses ─────────────────────────────────────────────────
//
// Anchors: data-model.md I-NEX-1, E-OBO; contracts C2.

// T007 witness 1 (I-NEX-1):
// When enable_next_expected_msg_seq_num is on, the initiator's outbound Logon
// MUST carry 789 == seqnum_mgr_.next_inbound_unsafe() (= 1 before any peer
// frame is received).
TEST(Emit, Initiator_AdvertisesNextExpectedInbound) {
    auto fix = make_initiator(std::make_shared<EmptyStoreFactory>(), /*enable_789=*/true);

    // The capture contains exactly the initiator Logon (emitted by open()).
    ASSERT_EQ(fix->capture.frames.size(), 1U) << "Expected exactly 1 outbound frame (Logon)";
    const auto& logon_frame = fix->capture.frames[0];

    // Tag 35 must be "A" (Logon).
    EXPECT_EQ(extract_tag(logon_frame, 35), "A");

    // Tag 789 MUST be present and equal to 1 (next_inbound_unsafe() before any
    // peer Logon = seqnum_min = 1 — no check_inbound has run yet).
    const std::string tag789 = extract_tag(logon_frame, 789);
    EXPECT_EQ(tag789, "1")
        << "Initiator Logon must carry 789=1 (next_inbound_unsafe()) when knob is on";
}

// T007 witness 2 (E-OBO, I-NEX-1):
// When enable_next_expected_msg_seq_num is on, the acceptor's REPLY Logon
// MUST carry 789 == next_inbound_unsafe() AFTER check_inbound has run
// (which advances next_inbound_ on a successful seq=1 Logon).
// With a peer Logon at seq=1, check_inbound(1) succeeds and next_inbound_ → 2.
// Therefore the reply Logon must carry 789=2 (NOT 789=3, i.e. NO extra +1).
//
// The E-OBO witness: 789 == 2 == the peer's actual next send after seq=1 Logon.
// If the implementation adds +1, we'd see 789=3, which is WRONG.
TEST(Emit, AcceptorReply_AdvertisesNextInboundNoPlusOne) {
    auto fix = make_acceptor(std::make_shared<EmptyStoreFactory>(), /*peer_logon_seq=*/1,
                             /*enable_789=*/true);

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    // The acceptor's reply Logon is the first (and only) outbound frame here.
    ASSERT_GE(fix->capture.frames.size(), 1U) << "Expected at least 1 outbound frame (reply Logon)";

    // Find the Logon (35=A) frame.
    auto it =
        std::find_if(fix->capture.frames.begin(), fix->capture.frames.end(),
                     [](const std::vector<std::byte>& f) { return extract_tag(f, 35) == "A"; });
    ASSERT_NE(it, fix->capture.frames.end()) << "Expected a Logon frame in outbound capture";

    // Tag 789 MUST be 2:
    // - check_inbound(1) advanced next_inbound_ to 2
    // - The reply Logon is built AFTER check_inbound (E-OBO: no extra +1 needed)
    // - Advertising 2 means "we have received all through 1; send us 2 next"
    // - This equals the peer's actual next expected send (seq=2 after its Logon at seq=1)
    const std::string tag789 = extract_tag(*it, 789);
    EXPECT_EQ(tag789, "2")
        << "Acceptor reply Logon must carry 789=2 (post-check_inbound next_inbound_unsafe()). "
           "No +1 needed because check_inbound already incremented. "
           "Got: 789="
        << tag789;
}

// ── T008 — US1 honor witnesses ────────────────────────────────────────────────
//
// Anchors: data-model.md I-NEX-2/3/11, contracts C4, RC#4 ordering.
//
// Setup: FIXPP_TEST_HOOKS (set_counters_for_test) seeds next_outbound to
// create a meaningful resend gap. The acceptor's reply Logon is sent at the
// seeded seq; after assign_outbound N = seeded + 1. The 789 honor runs AFTER
// the reply Logon is emitted (RC#4 ordering).
//
// I-NEX-11 guard: compare X against peek_outbound() (OUTBOUND), not
// next_inbound_unsafe() (INBOUND).

// T008 witness 1 (I-NEX-2/3, RC#4):
// Acceptor, knob on, store through 5, seeded outbound=6.
// Peer Logon seq=1, 789=3 (X=3 < N-at-decision).
// After reply Logon (seq=6, peek=7): N=7, X=3 → resend [3, eff_end=5].
// Assertions:
//   (a) At least 3 PossDup app frames at seqs 3,4,5 appear AFTER the reply Logon.
//   (b) Zero ResendRequest(35=2) frames emitted.
//   (c) Session reaches Active.
TEST(Honor, Acceptor_XltN_ResendsExactRange_AfterReply_NoResendRequest) {
    // Build acceptor fixture manually to allow counter seeding before Logon.
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(5);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Seed outbound counter to 6 so that:
    //   reply Logon is emitted at seq=6, peek_outbound becomes 7 (=N).
    //   ShortStore has OrderD frames at seqs 1..5 (our_last=5, eff_end=5).
    //   X=3 < N=7 → resend [3, eff_end=5].
    fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 6);

    // Feed peer Logon at seq=1 with 789=3.
    fix->feed(make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 3));

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Session must be Active after honoring 789";

    const auto& frames = fix->capture.frames;

    // (b) Zero ResendRequest(35=2) frames.
    for (const auto& f : frames) {
        EXPECT_NE(extract_tag(f, 35), "2") << "Honor_Acceptor: no ResendRequest must be emitted";
    }

    // Find the reply Logon frame (35=A).
    auto reply_it = std::find_if(frames.begin(), frames.end(), [](const std::vector<std::byte>& f) {
        return extract_tag(f, 35) == "A";
    });
    ASSERT_NE(reply_it, frames.end()) << "Expected reply Logon (35=A) in outbound capture";
    const std::size_t reply_idx = static_cast<std::size_t>(reply_it - frames.begin());

    // (a) PossDup frames at seqs 3,4,5 must appear AFTER the reply Logon (RC#4).
    // These are app frames (OrderD) replayed with 43=Y.
    const std::vector<std::uint32_t> expected_seqs = {3, 4, 5};
    for (std::uint32_t expected_seq : expected_seqs) {
        bool found = false;
        for (std::size_t i = reply_idx + 1; i < frames.size(); ++i) {
            const auto& f = frames[i];
            if (extract_tag(f, 34) == std::to_string(expected_seq) && extract_tag(f, 43) == "Y") {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected PossDup frame at seq=" << expected_seq
                           << " after reply Logon (RC#4 ordering)";
    }
}

// T008 witness 2 (I-NEX-2/3, C4/C8):
// Initiator, knob on, store through 4, seeded outbound=5 (after open()).
// Peer Logon-ack seq=1, 789=2 (X=2 < N-at-decision).
// After peer Logon-ack processing: N=5, X=2 → resend [2, eff_end=4].
// Assertions:
//   (a) At least 3 PossDup app frames at seqs 2,3,4.
//   (b) Zero ResendRequest(35=2) frames after the peer Logon-ack.
//   (c) Session reaches Active.
TEST(Honor, Initiator_XltN_ResendsExactRange_NoResendRequest) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};  // disable liveness
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(4);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    // open() emits the initiator Logon at seq=1 and transitions to LogonSent.
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

    // Seed outbound=5 so that at the 789 honor point:
    //   N = peek_outbound() = 5, X = 2 < 5 → resend [2, eff_end=4].
    fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 5);

    fix->clear_capture();

    // Feed peer Logon-ack at seq=1 with 789=2.
    fix->feed(make_logon_with_789("FIX.4.4", 1, "SRV", "CLI", 2));

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Session must be Active after processing Logon-ack";

    const auto& frames = fix->capture.frames;

    // (b) Zero ResendRequest(35=2) frames.
    for (const auto& f : frames) {
        EXPECT_NE(extract_tag(f, 35), "2") << "Honor_Initiator: no ResendRequest must be emitted";
    }

    // (a) PossDup frames at seqs 2,3,4 must be present.
    const std::vector<std::uint32_t> expected_seqs = {2, 3, 4};
    for (std::uint32_t expected_seq : expected_seqs) {
        bool found = false;
        for (const auto& f : frames) {
            if (extract_tag(f, 34) == std::to_string(expected_seq) && extract_tag(f, 43) == "Y") {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "Expected PossDup frame at seq=" << expected_seq;
    }
}

// T008 witness 3 (I-NEX-2):
// X == N ⇒ no resend (both roles).
// Acceptor: seeded outbound=4, peer sends 789=5 (X == N=5 after reply Logon).
// Initiator: seeded outbound=3, peer sends 789=3 (X == N=3).
TEST(Honor, XeqN_NoResend) {
    // ── Acceptor arm ─────────────────────────────────────────────────────────
    {
        auto fix = std::make_unique<Fixture>();
        fix->cfg.role = fixpp::session::session_role::acceptor;
        fix->cfg.sender_comp_id = "SRV";
        fix->cfg.target_comp_id = "CLI";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(5);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open_fut.get();

        // Seed outbound=4: reply Logon goes at seq=4 → peek_outbound=5=N.
        // Peer sends 789=5 → X=5 == N=5 → no resend.
        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 4);

        fix->feed(make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 5));

        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

        // No PossDup frames must be present (no resend).
        for (const auto& f : fix->capture.frames) {
            EXPECT_NE(extract_tag(f, 43), "Y")
                << "XeqN acceptor: no PossDup frame expected when X==N";
        }
        // No ResendRequest.
        for (const auto& f : fix->capture.frames) {
            EXPECT_NE(extract_tag(f, 35), "2") << "XeqN acceptor: no ResendRequest expected";
        }
    }

    // ── Initiator arm ─────────────────────────────────────────────────────────
    {
        auto fix2 = std::make_unique<Fixture>();
        fix2->cfg.role = fixpp::session::session_role::initiator;
        fix2->cfg.sender_comp_id = "CLI";
        fix2->cfg.target_comp_id = "SRV";
        fix2->cfg.begin_string = "FIX.4.4";
        fix2->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix2->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix2->cfg.heartbeat_interval = std::chrono::seconds{0};
        fix2->cfg.executor_override = fix2->ioc.get_executor();
        fix2->cfg.store_factory = std::make_shared<ShortStoreFactory>(3);
        fix2->cfg.reset_seqnum_policy_field =
            fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix2->cfg.enable_next_expected_msg_seq_num = true;
        fix2->cfg.transport_send = [&fix2 = *fix2](std::span<const std::byte> data) {
            fix2.capture(data);
        };
        fix2->session = std::make_unique<fixpp::session::Session>(fix2->eng, fix2->cfg);
        auto open_fut2 = asio::co_spawn(fix2->ioc, fix2->session->open(), asio::use_future);
        fix2->ioc.run_for(2s);
        fix2->ioc.restart();
        (void)open_fut2.get();
        ASSERT_EQ(fix2->session->state(), fixpp::session::fsm_state::LogonSent);

        // Seed outbound=3: at honor time N=3.
        // Peer sends 789=3 → X=3 == N=3 → no resend.
        fix2->session->seqnum_mgr_test_access().set_counters_for_test(1, 3);
        fix2->clear_capture();

        fix2->feed(make_logon_with_789("FIX.4.4", 1, "SRV", "CLI", 3));

        ASSERT_EQ(fix2->session->state(), fixpp::session::fsm_state::Active);

        for (const auto& f : fix2->capture.frames) {
            EXPECT_NE(extract_tag(f, 43), "Y")
                << "XeqN initiator: no PossDup frame expected when X==N";
        }
        for (const auto& f : fix2->capture.frames) {
            EXPECT_NE(extract_tag(f, 35), "2") << "XeqN initiator: no ResendRequest expected";
        }
    }
}

// T008 witness 4 — WalkExtraction TwoValueEnd (789-caller half):
// ExplicitEndBeyondStore: store through 2, seeded outbound=8, X=5 > our_last=2.
// 789 caller: replay_outbound_range_(5, N-1=8, through_current=true).
// Since begin=5 > eff_end=our_last=2 → early GapFill.
// NewSeqNo = peek_outbound() = 9 (NOT our_last+1=3 — guards through-current path).
//
// Anchors: data-model.md I-NEX-3, contracts C3.
TEST(WalkExtraction, TwoValueEnd_ExplicitEndBeyondStore_789Caller) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(2);  // store through 2
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Seed outbound=8: reply Logon at seq=8 → peek_outbound=9 at honor time (N=9).
    // Store through 2 (our_last=2). X=5 > our_last=2 → begin > eff_end → early GapFill.
    // GapFill NewSeqNo = peek_outbound() = 9 (NOT our_last+1=3).
    fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 8);

    fix->feed(make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 5));

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    const auto& frames = fix->capture.frames;
    auto gf_it = std::find_if(frames.begin(), frames.end(), frame_is_gapfill);
    ASSERT_NE(gf_it, frames.end()) << "Expected a SequenceReset-GapFill after 789 honor";

    // NewSeqNo must be peek_outbound()=9, NOT our_last+1=3.
    const std::string new_seqno = extract_tag(*gf_it, 36);
    EXPECT_EQ(new_seqno, "9")
        << "789-caller TwoValueEnd: GapFill NewSeqNo must be peek_outbound()=9 "
           "(through_current=true), not our_last+1=3";
}

// T008 witness 5 — WalkExtraction TwoValueEnd (789-caller half):
// EndSeqNo=0/through-current (789 caller always passes through_current=true),
// empty store. NewSeqNo = peek_outbound().
//
// Acceptor, EmptyStoreFactory, seeded outbound=5.
// Reply Logon at seq=5 → peek_outbound=6.
// X=1, N=6, replay_outbound_range_(1, 5, true) with empty store.
// → early GapFill, NewSeqNo = peek_outbound() = 6.
//
// Anchors: data-model.md I-NEX-3, contracts C3.
TEST(WalkExtraction, TwoValueEnd_EndSeqNo0_EmptyStore_789Caller) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Seed outbound=5: reply Logon at seq=5 → peek_outbound=6 at honor (N=6).
    // Empty store → our_last=0 → early GapFill, NewSeqNo=peek_outbound()=6.
    fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 5);

    fix->feed(make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 1));

    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    const auto& frames = fix->capture.frames;
    auto gf_it = std::find_if(frames.begin(), frames.end(), frame_is_gapfill);
    ASSERT_NE(gf_it, frames.end()) << "Expected a SequenceReset-GapFill (empty store)";

    const std::string new_seqno = extract_tag(*gf_it, 36);
    EXPECT_EQ(new_seqno, "6")
        << "789-caller EndSeqNo0-empty-store: GapFill NewSeqNo must be peek_outbound()=6";
}

// ── T009 — US1 behind-side / bidirectional / self-heal witnesses ──────────────
//
// Anchors: data-model.md I-NEX-5/10/12, research D-7/D-11/D-12,
//          contracts C5.
//
// These tests are RED until T016 implements behind-side tolerance in BOTH
// handlers. With T016 in place they should go GREEN.

// T009 witness 1a (I-NEX-5/12, D-7) — ACCEPTOR role:
// Knob on. Our next_inbound_=X=2 (we have seen seq 1). The peer's Logon
// arrives at seq=X_logon=5 (too-high: we expect 2). With T016 in place:
//   - NOT fatal at check_inbound (session stays Active, not Disconnected).
//   - next_inbound_ is left at X=2 (formulation A, no set_next_inbound).
//   - No at-logon ResendRequest emitted.
//   - The peer then resends [2, 3, 4, 5, 6] as PossDup frames (peer_N=7).
//   - After the fill: next_inbound_unsafe() == 7 (== peer_N, NOT X_logon+1=6).
//     peer_N=7 is set ≥1 frame beyond X_logon=5, so peer_N ≠ X_logon+1
//     and the assertion is not a coincidental pass (I-NEX-12).
//   - Zero ResendRequest on the wire.
TEST(BehindSide, KnobOn_AdmitsPeerResend_NoFatalDisconnect_Acceptor) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Seed inbound counter to X=2 (simulates: we have seen seq 1, expect 2 next).
    // next_outbound=1 (reply Logon will go at seq=1).
    fix->session->seqnum_mgr_test_access().set_counters_for_test(/*next_inbound=*/2,
                                                                 /*next_outbound=*/1);
    fix->clear_capture();

    // Feed peer Logon at seq=X_logon=5 (too-high from our next_inbound_=2).
    // With T016: session should tolerate, not fatal-disconnect.
    fix->feed(make_logon("FIX.4.4", /*seq=*/5, "CLI", "SRV"));

    // MUST reach Active (not Disconnected).
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "BehindSide acceptor: session must reach Active despite too-high peer Logon seqnum";

    // No ResendRequest (at-logon suppression, I-NEX-5).
    for (const auto& f : fix->capture.frames) {
        EXPECT_NE(extract_tag(f, 35), "2")
            << "BehindSide acceptor: no ResendRequest must be emitted at logon";
    }

    // Snapshot next_inbound_ before the peer's resend: should be X=2 still
    // (formulation A: left at X, not advanced for the held too-high Logon).
    const seqnum_t x_pre_resend = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(x_pre_resend, 2U)
        << "BehindSide acceptor: next_inbound_ must be X=2 (not advanced for held Logon)";

    // The peer's proactive resend: [2, 3, 4, 5, 6] (peer_N=7, ≥1 frame past X_logon=5).
    // Frames are PossDup app frames arriving in-sequence starting at X=2.
    // After all 5 frames, next_inbound_ must reach peer_N=7.
    fix->clear_capture();
    for (std::uint32_t seq = 2; seq <= 6; ++seq) {
        fix->feed(make_app_frame_posdup("FIX.4.4", seq, "CLI", "SRV"));
    }

    // Final counter check: next_inbound_ == peer_N = 7 (NOT X_logon+1=6).
    const seqnum_t final_next_inbound =
        fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(final_next_inbound, 7U)
        << "BehindSide acceptor: final next_inbound_ must be peer_N=7 (NOT X_logon+1=6). "
           "Counter was: "
        << final_next_inbound;

    // Session still Active after resend fill.
    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);
}

// T009 witness 1b (I-NEX-5/12, D-7) — INITIATOR role:
// Knob on. Our next_inbound_=X=3 (we expect seq 3 from peer). Peer's Logon-ack
// arrives at seq=X_logon=6 (too-high). With T016:
//   - NOT fatal at check_inbound; session reaches Active.
//   - next_inbound_ stays at X=3.
//   - No at-logon ResendRequest.
//   - Peer resends [3,4,5,6,7] (peer_N=8 ≥1 beyond X_logon=6).
//   - final next_inbound_==8 (NOT X_logon+1=7).
TEST(BehindSide, KnobOn_AdmitsPeerResend_NoFatalDisconnect_Initiator) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

    // Seed inbound counter to X=3 (simulates: peer sent 1,2 but we missed them).
    // next_outbound=2 (our Logon went at seq=1).
    fix->session->seqnum_mgr_test_access().set_counters_for_test(/*next_inbound=*/3,
                                                                 /*next_outbound=*/2);
    fix->clear_capture();

    // Feed peer Logon-ack at seq=X_logon=6 (too-high, we expect 3).
    // With T016: NOT fatal, session reaches Active.
    fix->feed(make_logon("FIX.4.4", /*seq=*/6, "SRV", "CLI"));

    // Must reach Active.
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "BehindSide initiator: session must reach Active despite too-high peer Logon seqnum";

    // No ResendRequest.
    for (const auto& f : fix->capture.frames) {
        EXPECT_NE(extract_tag(f, 35), "2")
            << "BehindSide initiator: no ResendRequest must be emitted at logon";
    }

    // next_inbound_ must be X=3 (not advanced for the held Logon).
    const seqnum_t x_pre_resend = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(x_pre_resend, 3U)
        << "BehindSide initiator: next_inbound_ must be X=3 (formulation A, no advance)";

    // Peer resends [3,4,5,6,7] (peer_N=8, ≥1 beyond X_logon=6).
    fix->clear_capture();
    for (std::uint32_t seq = 3; seq <= 7; ++seq) {
        fix->feed(make_app_frame_posdup("FIX.4.4", seq, "SRV", "CLI"));
    }

    // Final counter: next_inbound_ == peer_N = 8 (NOT X_logon+1=7).
    const seqnum_t final_next_inbound =
        fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(final_next_inbound, 8U)
        << "BehindSide initiator: final next_inbound_ must be peer_N=8 (NOT X_logon+1=7). "
           "Got: "
        << final_next_inbound;

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);
}

// T009 witness 2 (I-NEX-5/12, D-12) — Bidirectional:
// Both sides have a gap. Acceptor-side: next_inbound_=X=2, peer Logon too-high.
// Also seed next_outbound so the ACCEPTOR itself has frames to resend.
// (In this unit test we don't wire up a second live session; we verify
// OUR side's in-sequence fill from peer_N and the absence of ResendRequest.)
// Specifically: acceptor has outbound gap (peer will send 789=3, i.e., peer
// expects our seq 3), AND peer's Logon arrives at too-high seq (behind-side).
// Both recoveries happen without ResendRequest and without double-count.
TEST(BehindSide, Bidirectional_BothGaps_RecoverNoDoubleRecovery) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    // Store has [1..4] — so we can resend [3,4] when peer's 789=3.
    fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(4);
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // next_inbound_=2 (we've seen peer seq 1), next_outbound_=5 (we sent [1..4]).
    // Peer's Logon will carry 789=3 (peer expects OUR seq 3 = ahead-side gap).
    // Peer's Logon MsgSeqNum=6 (too-high = behind-side gap).
    // peer_N=8 (peer has sent through 7).
    fix->session->seqnum_mgr_test_access().set_counters_for_test(/*next_inbound=*/2,
                                                                 /*next_outbound=*/5);
    fix->clear_capture();

    // Feed peer Logon at seq=6 carrying 789=3 (BOTH gaps).
    fix->feed(make_logon_with_789("FIX.4.4", /*seq=*/6, "CLI", "SRV", /*789=*/3));

    // Must reach Active (behind-side tolerance + ahead-side resend).
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "Bidirectional: session must reach Active";

    // No ResendRequest from us (suppressed on both paths).
    for (const auto& f : fix->capture.frames) {
        EXPECT_NE(extract_tag(f, 35), "2")
            << "Bidirectional: no ResendRequest expected (789 path handles both gaps)";
    }

    // We should have emitted our resend [3,4] (ahead-side, from T014 honor path).
    bool found3 = false, found4 = false;
    for (const auto& f : fix->capture.frames) {
        if (extract_tag(f, 34) == "3" && extract_tag(f, 43) == "Y") found3 = true;
        if (extract_tag(f, 34) == "4" && extract_tag(f, 43) == "Y") found4 = true;
    }
    EXPECT_TRUE(found3) << "Bidirectional: expected PossDup resend at seq=3";
    EXPECT_TRUE(found4) << "Bidirectional: expected PossDup resend at seq=4";

    // Behind-side: next_inbound_ left at X=2 (not advanced for held Logon at seq=6).
    const seqnum_t x_pre = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(x_pre, 2U) << "Bidirectional: next_inbound_ must be X=2 after behind-side tolerance";

    // Now simulate the peer's in-sequence resend [2,3,4,5,6,7] (peer_N=8).
    fix->clear_capture();
    for (std::uint32_t seq = 2; seq <= 7; ++seq) {
        fix->feed(make_app_frame_posdup("FIX.4.4", seq, "CLI", "SRV"));
    }

    // final next_inbound_ == peer_N = 8.
    const seqnum_t final_ni = fix->session->seqnum_mgr_test_access().next_inbound_unsafe();
    EXPECT_EQ(final_ni, 8U) << "Bidirectional: final next_inbound_ must be peer_N=8. Got: "
                            << final_ni;

    // No double-count: no ResendRequest emitted by us during the fill.
    for (const auto& f : fix->capture.frames) {
        EXPECT_NE(extract_tag(f, 35), "2")
            << "Bidirectional: no ResendRequest during the in-sequence fill";
    }

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);
}

// T009 witness 3 (I-NEX-10, D-11) — LostResend self-heals via the Active arm:
// Knob on. Behind-side: session goes Active with next_inbound_=X=2. The peer
// fails to send its resend. The next live frame from the peer arrives at seq=6
// (which is too-high relative to our next_inbound_=2). The Active arm
// (`:1968-2009`) issues a ResendRequest — the recovery-of-last-resort path.
TEST(BehindSide, LostResend_SelfHealsViaActiveArm) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Seed: next_inbound_=2, next_outbound_=1. Peer Logon arrives at seq=5 (too-high).
    fix->session->seqnum_mgr_test_access().set_counters_for_test(/*next_inbound=*/2,
                                                                 /*next_outbound=*/1);
    fix->clear_capture();

    // Feed too-high peer Logon: with T016, session reaches Active, next_inbound_=2.
    fix->feed(make_logon("FIX.4.4", /*seq=*/5, "CLI", "SRV"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "LostResend: must reach Active with behind-side tolerance";

    // Verify no at-logon ResendRequest.
    for (const auto& f : fix->capture.frames) {
        EXPECT_NE(extract_tag(f, 35), "2") << "LostResend: no ResendRequest at logon (suppressed)";
    }

    // Now the peer skips the resend and sends a live frame at seq=6 (too-high
    // relative to next_inbound_=2). The Active arm should issue a ResendRequest.
    fix->clear_capture();
    fix->feed(make_fix_frame("FIX.4.4", "0", /*seq=*/6, "CLI", "SRV"));

    // The Active arm should have emitted a ResendRequest(35=2).
    bool rr_found = false;
    for (const auto& f : fix->capture.frames) {
        if (extract_tag(f, 35) == "2") {
            rr_found = true;
            break;
        }
    }
    EXPECT_TRUE(rr_found)
        << "LostResend: Active arm must emit a ResendRequest on the too-high live frame "
           "(recovery-of-last-resort, I-NEX-10/D-11)";
}

// ── T010 — US1 suppression + reset-table + no-heap witnesses ─────────────────
//
// Anchors: data-model.md I-NEX-5/8, research D-8, [const §VIII.5].

// T010 witness 1 (I-NEX-5) — Suppression contrast: knob ON vs OFF.
// Knob ON: peer Logon too-high at logon → tolerated (no fatal disconnect) AND
//   no at-logon ResendRequest (I-NEX-5). This is the 013 regression guard.
// Knob OFF: peer Logon too-high → fatal (existing 013 behavior: Disconnected).
TEST(Suppression, KnobOn_NoAtLogonResendRequest_KnobOff_FatalOnTooHigh) {
    // ── Arm 1: knob ON ────────────────────────────────────────────────────────
    {
        auto fix = std::make_unique<Fixture>();
        fix->cfg.role = fixpp::session::session_role::acceptor;
        fix->cfg.sender_comp_id = "SRV";
        fix->cfg.target_comp_id = "CLI";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open_fut.get();

        // next_inbound_=2; peer Logon arrives at seq=4 (too-high).
        fix->session->seqnum_mgr_test_access().set_counters_for_test(2, 1);
        fix->clear_capture();

        fix->feed(make_logon("FIX.4.4", 4, "CLI", "SRV"));

        // Knob ON: must reach Active (tolerated).
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "Suppression/KnobOn: must reach Active with too-high peer Logon when knob is on";

        // No ResendRequest at logon (suppressed by I-NEX-5).
        for (const auto& f : fix->capture.frames) {
            EXPECT_NE(extract_tag(f, 35), "2")
                << "Suppression/KnobOn: at-logon ResendRequest must be suppressed";
        }
    }

    // ── Arm 2: knob OFF — 013 regression guard ────────────────────────────────
    {
        auto fix2 = std::make_unique<Fixture>();
        fix2->cfg.role = fixpp::session::session_role::acceptor;
        fix2->cfg.sender_comp_id = "SRV";
        fix2->cfg.target_comp_id = "CLI";
        fix2->cfg.begin_string = "FIX.4.4";
        fix2->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix2->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix2->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix2->cfg.executor_override = fix2->ioc.get_executor();
        fix2->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
        fix2->cfg.reset_seqnum_policy_field =
            fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix2->cfg.enable_next_expected_msg_seq_num = false;  // knob OFF
        fix2->cfg.transport_send = [&fix2 = *fix2](std::span<const std::byte> data) {
            fix2.capture(data);
        };
        fix2->session = std::make_unique<fixpp::session::Session>(fix2->eng, fix2->cfg);
        auto open_fut2 = asio::co_spawn(fix2->ioc, fix2->session->open(), asio::use_future);
        fix2->ioc.run_for(1s);
        fix2->ioc.restart();
        (void)open_fut2.get();

        // next_inbound_=2; peer Logon at seq=4 (too-high).
        fix2->session->seqnum_mgr_test_access().set_counters_for_test(2, 1);
        fix2->clear_capture();

        fix2->feed(make_logon("FIX.4.4", 4, "CLI", "SRV"));

        // Knob OFF: must Disconnect (013 fatal-on-too-high intact).
        EXPECT_EQ(fix2->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Suppression/KnobOff: must Disconnect on too-high peer Logon (013 behavior)";
    }
}

// T010 witness 2a (I-NEX-8, D-8) — Reset table: initiator reset Logon → 789==1.
// Initiator with reset_on_logon=true emits a Logon with 141=Y and resets seqnums
// to 1 BEFORE building the Logon. With the knob on, 789 == next_inbound_unsafe()
// post-reset == 1. [Reset table row 1]
TEST(Reset, InitiatorResetLogon_Advertises1) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::initiator;
    fix->cfg.sender_comp_id = "CLI";
    fix->cfg.target_comp_id = "SRV";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{0};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.reset_on_logon = true;  // reset_on_logon knob
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    // Seed inbound/outbound to > 1 so the reset effect is observable.
    fix->session->seqnum_mgr_test_access().set_counters_for_test(/*next_inbound=*/5,
                                                                 /*next_outbound=*/4);

    // open() emits the initiator Logon (with reset_on_logon: reset fires BEFORE build).
    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(2s);
    fix->ioc.restart();
    (void)open_fut.get();
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

    // Find the Logon frame.
    ASSERT_GE(fix->capture.frames.size(), 1U);
    auto it =
        std::find_if(fix->capture.frames.begin(), fix->capture.frames.end(),
                     [](const std::vector<std::byte>& f) { return extract_tag(f, 35) == "A"; });
    ASSERT_NE(it, fix->capture.frames.end()) << "Expected Logon frame";

    // 789 must be 1 (post-reset next_inbound_unsafe() = 1). [Reset table row 1]
    const std::string tag789 = extract_tag(*it, 789);
    EXPECT_EQ(tag789, "1")
        << "Reset/InitiatorResetLogon: 789 must be 1 (post-reset next_inbound_). "
           "Got: 789="
        << tag789;

    // 141 must be Y (the reset Logon).
    EXPECT_EQ(extract_tag(*it, 141), "Y") << "Reset Logon must carry 141=Y";
}

// T010 witness 2b (I-NEX-8, D-8) — Reset table: acceptor reply with reset_on_logon → 789==2.
// reset_on_logon resets BEFORE check_inbound(1) which advances to 2. Reply built after →
// next_inbound_unsafe()=2. [Reset table row 2]
TEST(Reset, AcceptorReplyResetOnLogon_Advertises2) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.reset_on_logon = true;  // reset_on_logon knob
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon at seq=1 (reset path: reset_on_logon → reset to 1 → check_inbound(1) →
    // next_inbound=2).
    fix->feed(make_logon("FIX.4.4", /*seq=*/1, "CLI", "SRV"));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    // Find the reply Logon.
    auto it =
        std::find_if(fix->capture.frames.begin(), fix->capture.frames.end(),
                     [](const std::vector<std::byte>& f) { return extract_tag(f, 35) == "A"; });
    ASSERT_NE(it, fix->capture.frames.end()) << "Expected reply Logon";

    // 789 must be 2 (reset → 1 → check_inbound(1) → next_inbound=2 → advertise 2). [Reset table row
    // 2]
    const std::string tag789 = extract_tag(*it, 789);
    EXPECT_EQ(tag789, "2")
        << "Reset/AcceptorReplyResetOnLogon: 789 must be 2 (post-reset post-check_inbound). "
           "Got: 789="
        << tag789;
}

// T010 witness 2c (I-NEX-8, D-8) — Reset table: acceptor reply, received-141-only → 789==1.
// 013-only path: NO reset_on_logon, peer sends 141=Y. Reset fires AFTER check_inbound
// (at `:1718`) then BEFORE build. next_inbound_unsafe()=1 at build site. [Reset table row 3]
TEST(Reset, AcceptorReplyReceived141_Advertises1) {
    auto fix = std::make_unique<Fixture>();
    fix->cfg.role = fixpp::session::session_role::acceptor;
    fix->cfg.sender_comp_id = "SRV";
    fix->cfg.target_comp_id = "CLI";
    fix->cfg.begin_string = "FIX.4.4";
    fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    fix->cfg.heartbeat_interval = std::chrono::seconds{30};
    fix->cfg.executor_override = fix->ioc.get_executor();
    fix->cfg.store_factory = std::make_shared<EmptyStoreFactory>();
    // bilateral_lenient: accepts peer's 141=Y without requiring we also send it.
    fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = true;
    fix->cfg.reset_on_logon = false;  // NO knob-driven reset
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) { fix.capture(data); };
    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon at seq=1 with 141=Y (received-141-only path).
    // check_inbound(1) runs BEFORE the 013 reset (opposite of reset_on_logon).
    // After check_inbound: next_inbound_=2. Then reset → next_inbound_=1.
    // Build site sees next_inbound_unsafe()=1 → 789=1. [Reset table row 3]
    std::string extra;
    extra += field(98, "0");
    extra += field(108, "30");
    extra += field(141, "Y");
    fix->feed(make_fix_frame("FIX.4.4", "A", /*seq=*/1, "CLI", "SRV", extra));
    ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::Active);

    auto it =
        std::find_if(fix->capture.frames.begin(), fix->capture.frames.end(),
                     [](const std::vector<std::byte>& f) { return extract_tag(f, 35) == "A"; });
    ASSERT_NE(it, fix->capture.frames.end()) << "Expected reply Logon";

    // 789 must be 1 (post-received-141 reset, before build). [Reset table row 3]
    const std::string tag789 = extract_tag(*it, 789);
    EXPECT_EQ(tag789, "1")
        << "Reset/AcceptorReplyReceived141: 789 must be 1 (received-141-only path, post-reset). "
           "Got: 789="
        << tag789;
}

// T010 witness 3 ([const §VIII.5]) — NoHeap: build_logon with 789 append is zero-heap.
//
// Honest scope: the NEW synchronous no-heap surface this feature adds is the
// 789 emit append in build_logon (admin_messages.cpp). We measure that call
// directly between mallocnesia alloc_guard_start/alloc_guard_end markers, with
// all fixture setup (buffers, stack data) done OUTSIDE the guarded window.
//
// The proactive resend reuses replay_outbound_range_ (the single-implementation
// store-walk body extracted by T005). Its no-heap property is inherited from the
// existing recovery alloc-guard witness in tests/perf/
// (perf_session_recovery_alloc_guard / perf_session_recovery_alloc_guard_mallocnesia).
//
// The _mallocnesia ctest variant (CMakeLists.txt) runs this binary under
// LD_PRELOAD with tools/check_alloc.py so any global-heap escape aborts.
// [const §VIII.5], [[feedback_tracking_pmr_resource_false_pass]]
TEST(NoHeap, Emit789Append) {
    // ── Setup OUTSIDE the guarded window (legitimate allocation) ─────────────
    // Stack output buffer — same size session.cpp uses (256 bytes).
    std::array<std::byte, 256> out_buf{};

    // All inputs are string literals / integer constants: no heap.
    constexpr seqnum_t seq = 1;
    constexpr std::string_view sender = "SRV";
    constexpr std::string_view target = "CLI";
    constexpr std::string_view begin = "FIX.4.4";
    constexpr int heartbt = 30;
    constexpr std::string_view stime = "20240101-00:00:00.000";
    constexpr bool reset_seqnum = false;
    // next_expected_seq = 5 (non-nullopt → exercises the 789 append branch).
    const std::optional<seqnum_t> next_expected{5};

    // ── Guarded window: only the synchronous build_logon call ────────────────
    alloc_guard_start();

    auto result = fixpp::session::build_logon(std::span<std::byte>{out_buf.data(), out_buf.size()},
                                              seq, sender, target, begin, heartbt, stime,
                                              reset_seqnum, next_expected);

    alloc_guard_end();

    // ── Post-condition: call succeeded and the frame carries 789=5 ───────────
    ASSERT_TRUE(result.has_value()) << "build_logon with next_expected_seq=5 must succeed";

    // Scan the produced frame for "789=5\x01".
    const std::string frame_str(reinterpret_cast<const char*>(result->data()), result->size());
    EXPECT_NE(frame_str.find("789=5\x01"), std::string::npos)
        << "build_logon must emit tag 789=5 when next_expected_seq=5; "
           "frame="
        << frame_str;

    // Under mallocnesia: alloc_guard_count() returns the number of global
    // malloc/free calls in the window. A nonzero count means build_logon
    // touched the heap and the no-heap contract is violated. [const §VIII.5]
    const long heap_allocs = alloc_guard_count();
    EXPECT_EQ(heap_allocs, 0L)
        << "build_logon with 789 append must not touch the global heap; "
           "heap_allocs="
        << heap_allocs
        << "; [const §VIII.5 / I-NEX-7 / [[feedback_tracking_pmr_resource_false_pass]]]";
}

// ── T018 — US2 default-off byte-identical + inbound-789-ignored witness ──────
//
// Anchors: data-model.md I-NEX-7, contracts C9, FR-006/SC-002.
//
// (a) Knob OFF (default) ⇒ outbound Logon carries NO 789 field (byte-identical
//     to pre-feature baseline).
// (b) Knob OFF ⇒ an inbound Logon carrying 789 does NOT trigger the 789 honor
//     path — the session still reaches Active, but no proactive resend occurs.
//     Recovery uses the existing ResendRequest path (013 regression guard).
// (c) Contrast: knob ON ⇒ 789 IS emitted (positive control).
//
// NOTE: (b) also implicitly validates that with the knob off the session does
// not fatal-disconnect on an inbound 789 (i.e., the field is silently ignored).
TEST(DefaultOff, ByteIdenticalLogon_InboundIgnored) {
    // ── Part (a) + (c): outbound Logon byte-identity ─────────────────────────

    // Knob OFF (default): initiator Logon must NOT carry 789.
    {
        auto fix_off = make_initiator(std::make_shared<EmptyStoreFactory>(), /*enable_789=*/false);
        ASSERT_GE(fix_off->capture.frames.size(), 1U);
        const auto& logon_off = fix_off->capture.frames[0];
        EXPECT_EQ(extract_tag(logon_off, 35), "A");
        const std::string tag789_off = extract_tag(logon_off, 789);
        EXPECT_EQ(tag789_off, "")
            << "DefaultOff: knob OFF — initiator Logon must carry NO 789 field. "
               "Found 789="
            << tag789_off;
    }

    // Knob ON (positive control): initiator Logon MUST carry 789.
    {
        auto fix_on = make_initiator(std::make_shared<EmptyStoreFactory>(), /*enable_789=*/true);
        ASSERT_GE(fix_on->capture.frames.size(), 1U);
        const auto& logon_on = fix_on->capture.frames[0];
        const std::string tag789_on = extract_tag(logon_on, 789);
        EXPECT_NE(tag789_on, "")
            << "DefaultOff: knob ON — initiator Logon must carry 789 (positive control)";
    }

    // ── Part (b): knob OFF + inbound 789 ⇒ session reaches Active (field ignored).
    // Verify: (1) session state Active; (2) no proactive resend (no PossDup frames);
    // (3) no at-logon ResendRequest (039 suppression path not engaged — but also not
    //     the 789-honor path).
    // Store has [1..4] so if the 789 honor path ran it would resend [2,4] as PossDup.
    // X=2 < N=5 (seeded); if the honor path fires we'd see PossDup frames.
    {
        auto fix = std::make_unique<Fixture>();
        fix->cfg.role = fixpp::session::session_role::acceptor;
        fix->cfg.sender_comp_id = "SRV";
        fix->cfg.target_comp_id = "CLI";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(4);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = false;  // KNOB OFF
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open_fut.get();

        // Seed outbound=5 so N=5 at the decision point (if knob were on, X=2<N=5 → resend).
        fix->session->seqnum_mgr_test_access().set_counters_for_test(/*next_inbound=*/1,
                                                                     /*next_outbound=*/5);
        fix->clear_capture();

        // Feed inbound Logon with 789=2 (knob off ⇒ must be ignored).
        fix->feed(make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 2));

        // (1) Session must reach Active (inbound 789 silently ignored — no fatal).
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
            << "DefaultOff/InboundIgnored: knob OFF + inbound 789 must NOT fatal-disconnect. "
               "Session must reach Active.";

        // (2) No PossDup frames (789 honor path did NOT run).
        for (const auto& f : fix->capture.frames) {
            EXPECT_NE(extract_tag(f, 43), "Y")
                << "DefaultOff/InboundIgnored: knob OFF — no proactive resend must fire "
                   "(789 honor path must be silent when knob is off)";
        }

        // (3) No ResendRequest from us at logon time (neither 789-path nor 013 at-logon).
        for (const auto& f : fix->capture.frames) {
            EXPECT_NE(extract_tag(f, 35), "2")
                << "DefaultOff/InboundIgnored: knob OFF — no ResendRequest at logon";
        }
    }
}

// ── T020 — US3 integrity witnesses ───────────────────────────────────────────
//
// Anchors: data-model.md I-NEX-4/9, contracts C6, research D-6/D-10.
//
// Honor_XgtN_LogoutTextThenDisconnect:
//   Knob on, inbound 789 = X > N (our next-outbound) ⇒ Logout(text)+disconnect.
//   Session does NOT advance to Active/established.
//   Both acceptor and initiator roles.
//
// Honor_Invalid789_LogoutThenDisconnect:
//   Knob on, inbound 789 present-but-invalid — empty / non-digit / overflow —
//   ⇒ Logout+disconnect. Evaluated BEFORE the X<N compare (D-10).
//   Assert NO [1,N-1] full-history replay (no PossDup/GapFill flood).

// Helper: is this frame a Logout (35=5)?
static bool frame_is_logout(const std::vector<std::byte>& f) { return extract_tag(f, 35) == "5"; }

// Helper: make a Logon frame with a raw (possibly malformed) 789 value.
static std::vector<std::byte> make_logon_with_raw_789(std::string_view bs, std::uint32_t seq,
                                                      std::string_view s, std::string_view t,
                                                      std::string_view raw_789_value,
                                                      int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    // Build the 789 field with the raw (possibly invalid) value directly.
    extra += std::to_string(789) + "=" + std::string(raw_789_value) + "\x01";
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// T020 witness 1 (I-NEX-4, C6, D-6) — X > N: both roles.
TEST(Honor, XgtN_LogoutTextThenDisconnect) {
    // ── Acceptor arm ─────────────────────────────────────────────────────────
    // Store through 3 (our_last=3). Seed outbound=4 → reply Logon at seq=4 →
    // peek_outbound=5=N. Peer sends 789=7 (X=7 > N=5) → Logout+Disconnected.
    {
        auto fix = std::make_unique<Fixture>();
        fix->cfg.role = fixpp::session::session_role::acceptor;
        fix->cfg.sender_comp_id = "SRV";
        fix->cfg.target_comp_id = "CLI";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(3);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open_fut.get();

        // Seed outbound=4: reply Logon at seq=4 → peek_outbound=5=N.
        // X=7 > N=5 → must emit Logout then Disconnected.
        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 4);

        fix->feed(make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 7));

        // Session must be Disconnected.
        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Honor_XgtN acceptor: session must be Disconnected after X>N";

        // A Logout(35=5) must have been emitted.
        bool found_logout =
            std::any_of(fix->capture.frames.begin(), fix->capture.frames.end(), frame_is_logout);
        EXPECT_TRUE(found_logout)
            << "Honor_XgtN acceptor: Logout(35=5) must be emitted before disconnect";

        // NO PossDup frames — no [1,N-1] replay.
        for (const auto& f : fix->capture.frames) {
            EXPECT_NE(extract_tag(f, 43), "Y")
                << "Honor_XgtN acceptor: no PossDup resend must be emitted when X>N";
        }
        // NO GapFill frames.
        for (const auto& f : fix->capture.frames) {
            EXPECT_FALSE(frame_is_gapfill(f))
                << "Honor_XgtN acceptor: no GapFill must be emitted when X>N";
        }
    }

    // ── Initiator arm ─────────────────────────────────────────────────────────
    // Store through 2 (our_last=2). Seed outbound=3 → N=3 at honor time.
    // Peer sends 789=10 (X=10 > N=3) → Logout+Disconnected.
    // [gate-b/r1 FQ-1] Also assert: Active NEVER entered + onLogon NOT called.
    {
        auto app = std::make_shared<CountingApp027>();
        auto fix = std::make_unique<Fixture>();
        fix->eng.application = app;
        fix->cfg.role = fixpp::session::session_role::initiator;
        fix->cfg.sender_comp_id = "CLI";
        fix->cfg.target_comp_id = "SRV";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{0};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(2);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(2s);
        fix->ioc.restart();
        (void)open_fut.get();
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

        // Seed outbound=3: N=3 at honor time. X=10 > N=3.
        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 3);
        fix->clear_capture();

        fix->feed(make_logon_with_789("FIX.4.4", 1, "SRV", "CLI", 10));

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Honor_XgtN initiator: session must be Disconnected after X>N";

        bool found_logout =
            std::any_of(fix->capture.frames.begin(), fix->capture.frames.end(), frame_is_logout);
        EXPECT_TRUE(found_logout)
            << "Honor_XgtN initiator: Logout(35=5) must be emitted before disconnect";

        // No [1,N-1] replay.
        for (const auto& f : fix->capture.frames) {
            EXPECT_NE(extract_tag(f, 43), "Y")
                << "Honor_XgtN initiator: no PossDup resend when X>N";
        }
        for (const auto& f : fix->capture.frames) {
            EXPECT_FALSE(frame_is_gapfill(f)) << "Honor_XgtN initiator: no GapFill when X>N";
        }

        // [gate-b/r1 FQ-1 C6/C8 witness] — Active must NEVER have been entered.
        // The integrity error must be detected BEFORE the Active transition so that
        // onLogon is never fired and the FSM ring shows no Active visit.
        {
            auto hist = fix->session->fsm_visit_history();
            bool active_seen = std::any_of(
                hist.begin(), hist.end(),
                [](fixpp::session::fsm_state s) { return s == fixpp::session::fsm_state::Active; });
            EXPECT_FALSE(active_seen)
                << "Honor_XgtN initiator: Active must NEVER appear in fsm_visit_history() "
                   "on the X>N integrity path (C6 — MUST NOT advance to established as in-sync)";
        }
        EXPECT_EQ(app->onLogon_count, 0)
            << "Honor_XgtN initiator: onLogon must NOT be called on the X>N integrity path (C6)";
    }
}

// T020 witness 2 (I-NEX-9, C6, D-10) — invalid 789: empty / non-digit / overflow.
// Evaluated BEFORE X<N compare (D-10 security guard).
// Assert NO [1,N-1] replay for any invalid value.
TEST(Honor, Invalid789_LogoutThenDisconnect) {
    // Table of invalid raw 789 values.
    // overflow: a value larger than UINT32_MAX (4294967295).
    const std::vector<std::pair<std::string, std::string_view>> invalid_cases = {
        {"empty", ""},
        {"non-digit", "abc"},
        {"overflow", "99999999999"},  // > UINT32_MAX
    };

    for (const auto& [label, raw789] : invalid_cases) {
        // Test both acceptor and initiator for each invalid value.
        // ── Acceptor arm ─────────────────────────────────────────────────────
        {
            auto fix = std::make_unique<Fixture>();
            fix->cfg.role = fixpp::session::session_role::acceptor;
            fix->cfg.sender_comp_id = "SRV";
            fix->cfg.target_comp_id = "CLI";
            fix->cfg.begin_string = "FIX.4.4";
            fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
            fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
            fix->cfg.heartbeat_interval = std::chrono::seconds{30};
            fix->cfg.executor_override = fix->ioc.get_executor();
            fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(5);
            fix->cfg.reset_seqnum_policy_field =
                fixpp::session::reset_seqnum_policy::bilateral_lenient;
            fix->cfg.enable_next_expected_msg_seq_num = true;
            fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
                fix.capture(data);
            };
            fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
            auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
            fix->ioc.run_for(1s);
            fix->ioc.restart();
            (void)open_fut.get();

            // Seed outbound=6 so N=7 after reply Logon.
            // If invalid-789 fell through to X<N branch (D-10 guard missing), the
            // parse→0 would clamp begin to 1 and replay [1, N-1=6] — PossDup flood.
            fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 6);

            fix->feed(make_logon_with_raw_789("FIX.4.4", 1, "CLI", "SRV", raw789));

            EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
                << "Honor_Invalid789 acceptor [" << label << "]: must be Disconnected";

            bool found_logout = std::any_of(fix->capture.frames.begin(), fix->capture.frames.end(),
                                            frame_is_logout);
            EXPECT_TRUE(found_logout)
                << "Honor_Invalid789 acceptor [" << label << "]: Logout must be emitted";

            // NO [1,N-1] replay — the D-10 guard must fire BEFORE X<N.
            for (const auto& f : fix->capture.frames) {
                EXPECT_NE(extract_tag(f, 43), "Y")
                    << "Honor_Invalid789 acceptor [" << label
                    << "]: "
                       "no PossDup resend — D-10 invalid-first guard must block X<N";
            }
            for (const auto& f : fix->capture.frames) {
                EXPECT_FALSE(frame_is_gapfill(f))
                    << "Honor_Invalid789 acceptor [" << label
                    << "]: "
                       "no GapFill — D-10 invalid-first guard must block X<N";
            }
        }

        // ── Initiator arm ─────────────────────────────────────────────────────
        // [gate-b/r1 FQ-1] Also assert: Active NEVER entered + onLogon NOT called.
        {
            auto app = std::make_shared<CountingApp027>();
            auto fix = std::make_unique<Fixture>();
            fix->eng.application = app;
            fix->cfg.role = fixpp::session::session_role::initiator;
            fix->cfg.sender_comp_id = "CLI";
            fix->cfg.target_comp_id = "SRV";
            fix->cfg.begin_string = "FIX.4.4";
            fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
            fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
            fix->cfg.heartbeat_interval = std::chrono::seconds{0};
            fix->cfg.executor_override = fix->ioc.get_executor();
            fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(4);
            fix->cfg.reset_seqnum_policy_field =
                fixpp::session::reset_seqnum_policy::bilateral_lenient;
            fix->cfg.enable_next_expected_msg_seq_num = true;
            fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
                fix.capture(data);
            };
            fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
            auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
            fix->ioc.run_for(2s);
            fix->ioc.restart();
            (void)open_fut.get();
            ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

            // Seed outbound=5 so N=5 at honor time. Store has [1..4].
            fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 5);
            fix->clear_capture();

            fix->feed(make_logon_with_raw_789("FIX.4.4", 1, "SRV", "CLI", raw789));

            EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
                << "Honor_Invalid789 initiator [" << label << "]: must be Disconnected";

            bool found_logout = std::any_of(fix->capture.frames.begin(), fix->capture.frames.end(),
                                            frame_is_logout);
            EXPECT_TRUE(found_logout)
                << "Honor_Invalid789 initiator [" << label << "]: Logout must be emitted";

            // NO [1,N-1] replay.
            for (const auto& f : fix->capture.frames) {
                EXPECT_NE(extract_tag(f, 43), "Y")
                    << "Honor_Invalid789 initiator [" << label
                    << "]: "
                       "no PossDup resend — D-10 invalid-first guard";
            }
            for (const auto& f : fix->capture.frames) {
                EXPECT_FALSE(frame_is_gapfill(f)) << "Honor_Invalid789 initiator [" << label
                                                  << "]: "
                                                     "no GapFill — D-10 invalid-first guard";
            }

            // [gate-b/r1 FQ-1 C6/C8 witness] — Active must NEVER have been entered.
            {
                auto hist = fix->session->fsm_visit_history();
                bool active_seen =
                    std::any_of(hist.begin(), hist.end(), [](fixpp::session::fsm_state s) {
                        return s == fixpp::session::fsm_state::Active;
                    });
                EXPECT_FALSE(active_seen)
                    << "Honor_Invalid789 initiator [" << label
                    << "]: "
                       "Active must NEVER appear in fsm_visit_history() on the invalid-789 path "
                       "(C6 — MUST NOT advance to established as in-sync)";
            }
            EXPECT_EQ(app->onLogon_count, 0)
                << "Honor_Invalid789 initiator [" << label
                << "]: "
                   "onLogon must NOT be called on the invalid-789 path (C6)";
        }
    }
}

// ── FQ-2 witnesses: 789-Logout paths route through fire_to_admin_ ────────────
//
// T020/T021 — every engine-originated admin emit calls toAdmin (FR-008/010).
// Before gate-b/r1 FQ-2 fix, the two honor_peer_next_expected_() Logout sites
// (invalid-789 and X>N) bypassed fire_to_admin_. Post-fix they call it.
//
// Two sub-tests per arm:
//   (a) observe: toAdmin IS invoked for the 789-triggered Logout frame.
//   (b) throw: a throwing toAdmin surfaces app_callback_threw + terminal-close.
//
// Covers both acceptor and initiator (C8 symmetry).

// ── Sub-test (a): toAdmin IS observed for the X>N Logout — both roles ─────────
TEST(Honor, Integrity_FiresToAdmin_XgtN) {
    // Acceptor arm.
    {
        auto app = std::make_shared<CountingApp027>();
        auto fix = std::make_unique<Fixture>();
        fix->eng.application = app;
        fix->cfg.role = fixpp::session::session_role::acceptor;
        fix->cfg.sender_comp_id = "SRV";
        fix->cfg.target_comp_id = "CLI";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(3);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open_fut.get();

        // outbound=4 → reply Logon seq=4 → peek_outbound=5=N. X=9 > N=5.
        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 4);

        // toAdmin call #1: acceptor reply Logon. toAdmin call #2: the 789-Logout.
        fix->feed(make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 9));

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Integrity_FiresToAdmin acceptor X>N: must disconnect";
        // toAdmin must have been called at least twice (reply Logon + 789-Logout).
        EXPECT_GE(app->toAdmin_count, 2)
            << "Integrity_FiresToAdmin acceptor X>N: toAdmin must be called for the 789-Logout "
               "(FR-008 — every engine-originated admin emit calls toAdmin)";
    }

    // Initiator arm.
    {
        auto app = std::make_shared<CountingApp027>();
        auto fix = std::make_unique<Fixture>();
        fix->eng.application = app;
        fix->cfg.role = fixpp::session::session_role::initiator;
        fix->cfg.sender_comp_id = "CLI";
        fix->cfg.target_comp_id = "SRV";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{0};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(2);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(2s);
        fix->ioc.restart();
        (void)open_fut.get();
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

        // toAdmin call #1: outbound Logon (emitted by open()). N=3 at honor. X=7 > N=3.
        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 3);
        fix->clear_capture();

        fix->feed(make_logon_with_789("FIX.4.4", 1, "SRV", "CLI", 7));

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Integrity_FiresToAdmin initiator X>N: must disconnect";
        // toAdmin call #1 was the outbound Logon. The 789-Logout must add at least one more.
        EXPECT_GE(app->toAdmin_count, 2)
            << "Integrity_FiresToAdmin initiator X>N: toAdmin must be called for the 789-Logout "
               "(FR-008 — every engine-originated admin emit calls toAdmin)";
    }
}

// ── Sub-test (b): toAdmin IS observed for the invalid-789 Logout — both roles ─
TEST(Honor, Integrity_FiresToAdmin_Invalid789) {
    // Acceptor arm: invalid 789 (empty string).
    {
        auto app = std::make_shared<CountingApp027>();
        auto fix = std::make_unique<Fixture>();
        fix->eng.application = app;
        fix->cfg.role = fixpp::session::session_role::acceptor;
        fix->cfg.sender_comp_id = "SRV";
        fix->cfg.target_comp_id = "CLI";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(3);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open_fut.get();

        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 4);

        fix->feed(make_logon_with_raw_789("FIX.4.4", 1, "CLI", "SRV", "GARBAGE"));

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Integrity_FiresToAdmin acceptor Invalid789: must disconnect";
        EXPECT_GE(app->toAdmin_count, 2)
            << "Integrity_FiresToAdmin acceptor Invalid789: toAdmin must be called for "
               "the invalid-789 Logout (FR-008)";
    }

    // Initiator arm: invalid 789 (empty string).
    {
        auto app = std::make_shared<CountingApp027>();
        auto fix = std::make_unique<Fixture>();
        fix->eng.application = app;
        fix->cfg.role = fixpp::session::session_role::initiator;
        fix->cfg.sender_comp_id = "CLI";
        fix->cfg.target_comp_id = "SRV";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{0};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(2);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(2s);
        fix->ioc.restart();
        (void)open_fut.get();
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 3);
        fix->clear_capture();

        fix->feed(make_logon_with_raw_789("FIX.4.4", 1, "SRV", "CLI", "GARBAGE"));

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Integrity_FiresToAdmin initiator Invalid789: must disconnect";
        EXPECT_GE(app->toAdmin_count, 2)
            << "Integrity_FiresToAdmin initiator Invalid789: toAdmin must be called for "
               "the invalid-789 Logout (FR-008)";
    }
}

// ── Sub-test (c): throwing toAdmin on X>N Logout surfaces app_callback_threw ──
//
// ThrowingToAdmin027 throws on the N-th toAdmin call.
// For the initiator: call #1 = outbound Logon; call #2 = the 789-Logout.
// For the acceptor: call #1 = reply Logon; call #2 = the 789-Logout.
TEST(Honor, Integrity_ToAdminThrow_SurfacesAppCallbackThrew) {
    // Acceptor arm: ThrowingToAdmin027(2) — throw on the 789-Logout toAdmin.
    {
        auto app = std::make_shared<ThrowingToAdmin027>(2);
        auto fix = std::make_unique<Fixture>();
        fix->eng.application = app;
        fix->cfg.role = fixpp::session::session_role::acceptor;
        fix->cfg.sender_comp_id = "SRV";
        fix->cfg.target_comp_id = "CLI";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{30};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(3);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(1s);
        fix->ioc.restart();
        (void)open_fut.get();

        // outbound=4 → reply Logon seq=4 → N=5. X=9 > N=5 → 789-Logout → toAdmin(2) throws.
        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 4);

        // Frame must outlive ioc.run_for (on_inbound_frame holds a span into it).
        const auto logon_frame = make_logon_with_789("FIX.4.4", 1, "CLI", "SRV", 9);
        auto fut = asio::co_spawn(
            fix->ioc, fix->session->on_inbound_frame(std::span<const std::byte>(logon_frame)),
            asio::use_future);
        fix->ioc.run_for(5s);
        fix->ioc.restart();
        auto result = fut.get();

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Integrity_ToAdminThrow acceptor X>N: must be Disconnected after toAdmin throw";
        ASSERT_FALSE(result.has_value())
            << "Integrity_ToAdminThrow acceptor X>N: on_inbound_frame must return an error";
        EXPECT_EQ(result.error(), fixpp::core::error::app_callback_threw)
            << "Integrity_ToAdminThrow acceptor X>N: error must be app_callback_threw (130)";
        EXPECT_EQ(app->call_count, 2)
            << "Integrity_ToAdminThrow acceptor X>N: toAdmin must have been called exactly twice "
               "(call 1: reply Logon; call 2: 789-Logout that threw)";
    }

    // Initiator arm: ThrowingToAdmin027(2) — throw on the 789-Logout toAdmin.
    {
        auto app = std::make_shared<ThrowingToAdmin027>(2);
        auto fix = std::make_unique<Fixture>();
        fix->eng.application = app;
        fix->cfg.role = fixpp::session::session_role::initiator;
        fix->cfg.sender_comp_id = "CLI";
        fix->cfg.target_comp_id = "SRV";
        fix->cfg.begin_string = "FIX.4.4";
        fix->cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        fix->cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        fix->cfg.heartbeat_interval = std::chrono::seconds{0};
        fix->cfg.executor_override = fix->ioc.get_executor();
        fix->cfg.store_factory = std::make_shared<ShortStoreFactory>(2);
        fix->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        fix->cfg.enable_next_expected_msg_seq_num = true;
        fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
            fix.capture(data);
        };
        fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);
        auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
        fix->ioc.run_for(2s);
        fix->ioc.restart();
        (void)open_fut.get();
        ASSERT_EQ(fix->session->state(), fixpp::session::fsm_state::LogonSent);

        // call #1 = outbound Logon (fired during open()). N=3. X=7 > N=3 → 789-Logout → throw.
        fix->session->seqnum_mgr_test_access().set_counters_for_test(1, 3);
        fix->clear_capture();

        // Frame must outlive ioc.run_for (on_inbound_frame holds a span into it).
        const auto logon_frame = make_logon_with_789("FIX.4.4", 1, "SRV", "CLI", 7);
        auto fut = asio::co_spawn(
            fix->ioc, fix->session->on_inbound_frame(std::span<const std::byte>(logon_frame)),
            asio::use_future);
        fix->ioc.run_for(5s);
        fix->ioc.restart();
        auto result = fut.get();

        EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Disconnected)
            << "Integrity_ToAdminThrow initiator X>N: must be Disconnected after toAdmin throw";
        ASSERT_FALSE(result.has_value())
            << "Integrity_ToAdminThrow initiator X>N: on_inbound_frame must return an error";
        EXPECT_EQ(result.error(), fixpp::core::error::app_callback_threw)
            << "Integrity_ToAdminThrow initiator X>N: error must be app_callback_threw (130)";
        EXPECT_EQ(app->call_count, 2)
            << "Integrity_ToAdminThrow initiator X>N: toAdmin must have been called exactly twice "
               "(call 1: outbound Logon; call 2: 789-Logout that threw)";
    }
}

}  // namespace
