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

namespace {

// ── Frame-building helpers (mirror test_recovery_*.cpp pattern) ───────────────

static std::string field(int tag, std::string_view val)
{
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

static std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type,
                                             std::uint32_t seq,
                                             std::string_view sender,
                                             std::string_view target,
                                             std::string_view extra = {})
{
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
                                         int hbt = 30)
{
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

static std::vector<std::byte> make_resend_request(std::string_view bs, std::uint32_t seq,
                                                  std::string_view s, std::string_view t,
                                                  std::uint32_t begin_seqno,
                                                  std::uint32_t end_seqno)
{
    std::string extra;
    extra += field(7, std::to_string(begin_seqno));
    extra += field(16, std::to_string(end_seqno));
    return make_fix_frame(bs, "2", seq, s, t, extra);
}

// ── Outbound capture ──────────────────────────────────────────────────────────

struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    void operator()(std::span<const std::byte> data)
    {
        frames.emplace_back(data.begin(), data.end());
    }
};

// ── Frame field extractors ────────────────────────────────────────────────────

static std::string extract_tag(const std::vector<std::byte>& frame, int tag)
{
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

static bool frame_is_gapfill(const std::vector<std::byte>& frame)
{
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
        : MessageStore(flush_thunk_for<ShortStore>()), next_outbound_(last_stored + 1U)
    {
        for (seqnum_t k = 1; k <= last_stored; ++k) {
            const auto raw = make_fix_frame("FIX.4.4", "D", k, "SRV", "CLI",
                                            field(11, "ORD" + std::to_string(k)));
            frames_[k] = raw;
        }
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t /*seq*/, std::span<const std::byte> /*frame*/,
        direction_t /*dir*/) noexcept override
    {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor) noexcept override
    {
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
        direction_t dir, bool /*increment*/) noexcept override
    {
        co_return dir == direction_t::outbound ? next_outbound_ : seqnum_t{1};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override
    {
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
        std::string_view /*sender*/, std::string_view /*target*/,
        std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override
    {
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
        direction_t /*dir*/) noexcept override
    {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t /*begin*/, seqnum_t /*end*/, direction_t /*dir*/,
        retrieve_visitor& /*visitor*/) noexcept override
    {
        co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t /*dir*/, bool /*increment*/) noexcept override
    {
        co_return seqnum_t{1};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override
    {
        co_return fixpp::core::expected_t<void>{};
    }
};

class EmptyStoreFactory final : public MessageStoreFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/,
        std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override
    {
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

    void feed(const std::vector<std::byte>& frame)
    {
        auto fut = asio::co_spawn(
            ioc, session->on_inbound_frame(std::span<const std::byte>(frame)),
            asio::use_future);
        ioc.run_for(5s);
        ioc.restart();
        (void)fut.get();
    }

    void clear_capture() { capture.frames.clear(); }
};

static std::unique_ptr<Fixture> make_acceptor(
    std::shared_ptr<MessageStoreFactory> store_factory,
    std::uint32_t peer_logon_seq = 1)
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
    fix->cfg.store_factory = std::move(store_factory);
    // bilateral_lenient: accept peer Logon without requiring matching 141=Y.
    fix->cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
        fix.capture(data);
    };

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

static std::unique_ptr<Fixture> make_initiator(
    std::shared_ptr<MessageStoreFactory> store_factory,
    bool enable_789 = false)
{
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
    fix->cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = enable_789;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
        fix.capture(data);
    };

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

// Builds an acceptor Session with the knob controlled separately, feeding peer
// Logon at a given seq, but does NOT complete the handshake (caller can inspect
// before or after the Logon).
static std::unique_ptr<Fixture> make_acceptor_knob(
    std::shared_ptr<MessageStoreFactory> store_factory,
    bool enable_789,
    std::uint32_t peer_logon_seq = 1)
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
    fix->cfg.store_factory = std::move(store_factory);
    fix->cfg.reset_seqnum_policy_field =
        fixpp::session::reset_seqnum_policy::bilateral_lenient;
    fix->cfg.enable_next_expected_msg_seq_num = enable_789;
    fix->cfg.transport_send = [&fix = *fix](std::span<const std::byte> data) {
        fix.capture(data);
    };

    fix->session = std::make_unique<fixpp::session::Session>(fix->eng, fix->cfg);

    auto open_fut = asio::co_spawn(fix->ioc, fix->session->open(), asio::use_future);
    fix->ioc.run_for(1s);
    fix->ioc.restart();
    (void)open_fut.get();

    // Feed peer Logon to reach Active.
    fix->feed(make_logon("FIX.4.4", peer_logon_seq, "CLI", "SRV"));

    EXPECT_EQ(fix->session->state(), fixpp::session::fsm_state::Active)
        << "make_acceptor_knob: session must be Active after Logon";

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
TEST(WalkExtraction, TwoValueEnd_ExplicitEndBeyondStore)
{
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
TEST(WalkExtraction, TwoValueEnd_EndSeqNo0_EmptyStore)
{
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
TEST(WalkExtraction, SingleImplementation)
{
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
    EXPECT_EQ(loop_count, 1)
        << "Store-walk loop must exist exactly once in session.cpp "
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
TEST(Emit, Initiator_AdvertisesNextExpectedInbound)
{
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
TEST(Emit, AcceptorReply_AdvertisesNextInboundNoPlusOne)
{
    auto fix = make_acceptor_knob(std::make_shared<EmptyStoreFactory>(), /*enable_789=*/true,
                                  /*peer_logon_seq=*/1);

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
           "Got: 789=" << tag789;
}

}  // namespace
