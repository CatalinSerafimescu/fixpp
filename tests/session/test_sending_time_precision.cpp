// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_sending_time_precision.cpp
//
// 026-nanosecond-sendingtime (S-039) — session emit/inbound precision witnesses.
// T005 session emit RED-first witnesses (US1):
//   (4) SendingTimePrecision_Nanos_Emits27Char52
//   (5) SendingTimePrecision_Micros_Emits24Char52
//   (6) SendingTimePrecision_DefaultMillis_ByteIdentical
// T012 session inbound witnesses (US2):
//   (4) InboundNanos52_ParsedNotRejected
//   (5) MaxLatency_OnNanosInbound_BoundaryCorrect
//   (6) OrigSendingTime122_PreservedVerbatim_OnResend
//
// RED rationale (T006): every stamp site hardcodes millis; cfg_.sending_time_precision
// is never read until T009 lands. (4) and (5) fail because 52= is always 21 chars;
// (6) must ALREADY PASS (unchanged path = zero-regression gate SC-002 / FR-003 / I-NST-1).
//
// Anchors: contracts/sendingtime-precision.md C5; data-model E5 / E6 / I-NST-1 / I-NST-6;
//          tasks T005 / T006 / T009 / T012.

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/fix_time.hpp>
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
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "session/support/frame_field_extract.hpp"   // via -I tests/
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;
using fixpp::session::test_support::extract_field;

namespace fixpp::session::test {

// ── CapturingStore — real store+retrieve for the resend-independence witness ──
//
// Mirrors the W7 store from test_send_allow_pos_dup_strip.cpp.
// Required so the ResendRequest path in session.cpp can retrieve stored frames
// (a null/default store returns nothing, so build_replay_frame never fires).

class CapturingStore126 final : public MessageStore {
public:
    struct Record {
        seqnum_t seq;
        std::vector<std::byte> frame;
    };
    std::vector<Record> outbound_records;

    explicit CapturingStore126() noexcept : MessageStore(flush_thunk_for<CapturingStore126>()) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        if (dir == direction_t::outbound) {
            outbound_records.push_back({seq, std::vector<std::byte>(frame.begin(), frame.end())});
            if (seq + 1U > next_out_) next_out_ = seq + 1U;
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t from, seqnum_t to, direction_t dir, retrieve_visitor& visitor) noexcept override {
        if (dir == direction_t::outbound) {
            for (auto& rec : outbound_records) {
                if (rec.seq >= from && rec.seq <= to) {
                    auto r = co_await visitor.on_frame(
                        rec.seq, std::span<const std::byte>(rec.frame));
                    if (!r || *r == visit_result::stop) break;
                }
            }
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        auto& c = (dir == direction_t::outbound) ? next_out_ : next_in_;
        const seqnum_t curr = c;
        if (increment) ++c;
        co_return curr;
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        next_in_ = next_out_ = seqnum_min;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    seqnum_t next_out_ = seqnum_min;
    seqnum_t next_in_ = seqnum_min;
};

class CapturingStoreFactory126 final : public MessageStoreFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<CapturingStore126>();
    }
};

// ── Frame builders ────────────────────────────────────────────────────────────

static std::vector<std::byte> make_fix44_frame(std::string_view body_str) {
    std::string hdr = "8=FIX.4.4\x01";
    hdr += "9=" + std::to_string(body_str.size()) + "\x01";
    std::string full = hdr + std::string(body_str);
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFu;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Peer Logon (35=A) from TW → ISLD (acceptor role).
static std::vector<std::byte> make_peer_logon(std::uint32_t seq = 1) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=ISLD\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    return make_fix44_frame(body);
}

// Build a FIX 4.4 frame with a caller-supplied 52= value (for inbound frames).
static std::vector<std::byte> make_fix44_frame_with_time(std::string_view msg_type,
                                                          std::uint32_t seq,
                                                          std::string_view sender,
                                                          std::string_view target,
                                                          std::string_view sending_time,
                                                          std::string extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=" + std::string(sending_time) + "\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) body += extra_fields;

    std::string hdr = "8=FIX.4.4\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFu;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Build a ResendRequest(35=2) inbound peer frame.
static std::vector<std::byte> make_resend_request_44(std::uint32_t seq,
                                                      std::string_view sender,
                                                      std::string_view target,
                                                      std::uint32_t begin_seqno,
                                                      std::uint32_t end_seqno) {
    std::string extra;
    extra += "7=" + std::to_string(begin_seqno) + "\x01";
    extra += "16=" + std::to_string(end_seqno) + "\x01";
    return make_fix44_frame_with_time("2", seq, sender, target, "20240101-00:00:00.000", extra);
}

// ── Fixture ────────────────────────────────────────────────────────────────────
//
// Acceptor session (ISLD ← TW) that captures every outbound frame.
// The fixture sets up the mock clock with a concrete nanosecond-resolution
// time point so the 52= value is deterministic.

class SendingTimePrecisionTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    // All outbound frames captured by transport_send.
    std::vector<std::vector<std::byte>> captured_frames;

    void SetUp() override {
        using namespace std::chrono;
        // Clock returns 2024-01-01T00:00:00.123456789Z (ns-resolution).
        // millis form: "20240101-00:00:00.123" (21 chars)
        // micros form: "20240101-00:00:00.123456" (24 chars)
        // nanos  form: "20240101-00:00:00.123456789" (27 chars)
        const auto utc =
            system_clock::time_point{seconds{1704067200}} + nanoseconds{123456789};
        const auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Build a session config with the given precision and optional store factory.
    // Acceptor role so drive_to_active() feeds a peer Logon.
    SessionConfig make_cfg(
        fixpp::core::fix_time_precision prec = fixpp::core::fix_time_precision::millis,
        std::shared_ptr<MessageStoreFactory> store_factory = nullptr) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.sending_time_precision = prec;
        cfg.store_factory = std::move(store_factory);
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    void run_ioc() {
        ioc.run_for(200ms);
        ioc.restart();
    }

    // Open acceptor session + feed peer Logon → Active.
    // Clears captured_frames after reaching Active.
    void drive_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_peer_logon(1);
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut2.get().has_value()) << "peer Logon feed failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
        // Discard the open() + logon-ack frames; witnesses assert only on subsequent frames.
        captured_frames.clear();
    }

    // Return the value of field 52 from the FIRST captured frame (after any drive_to_active flush).
    // Returns nullopt if not present.
    std::optional<std::string> first_frame_tag52() const {
        if (captured_frames.empty()) return std::nullopt;
        const auto& frame = captured_frames.front();
        auto field = extract_field(std::span<const std::byte>(frame), 52);
        if (!field) return std::nullopt;
        return std::string(*field);
    }

    // Return the value of field 52 from the LAST captured frame.
    std::optional<std::string> last_frame_tag52() const {
        if (captured_frames.empty()) return std::nullopt;
        const auto& frame = captured_frames.back();
        auto field = extract_field(std::span<const std::byte>(frame), 52);
        if (!field) return std::nullopt;
        return std::string(*field);
    }
};

// ── (4) T005: nanos → 52= is 27 chars ────────────────────────────────────────
//
// Session with sending_time_precision = nanos → the Logon reply's 52= must be
// 27 chars (YYYYMMDD-HH:MM:SS.sssssssss). [contract C5 / SC-001 / data-model E6]
//
// RED: the Logon reply is the first captured frame. With no threading (T009 not
// landed), stamp sites hardcode millis → 52= is 21 chars → FAILS.
TEST_F(SendingTimePrecisionTest, SendingTimePrecision_Nanos_Emits27Char52) {
    auto cfg = make_cfg(fixpp::core::fix_time_precision::nanos);
    Session sess(engine, cfg);

    // Capture the Logon reply — it is the first emitted frame for an acceptor.
    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";

    auto logon = make_peer_logon(1);
    auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut2.get().has_value()) << "peer Logon feed failed";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // The Logon reply (35=A) must carry a 27-char 52= field.
    ASSERT_FALSE(captured_frames.empty()) << "no frames emitted during open/logon";

    // Find the frame with 35=A (the Logon reply).
    bool found_logon_reply = false;
    for (const auto& frame : captured_frames) {
        auto tag35 = extract_field(std::span<const std::byte>(frame), 35);
        if (!tag35 || *tag35 != "A") continue;
        found_logon_reply = true;

        auto tag52 = extract_field(std::span<const std::byte>(frame), 52);
        ASSERT_TRUE(tag52.has_value())
            << "Logon reply must contain tag 52 (SendingTime)";
        EXPECT_EQ(tag52->size(), 27u)
            << "nanos config must emit 27-char SendingTime; got '" << *tag52 << "' (len="
            << tag52->size() << "); [contract C5 / SC-001 / data-model E6]";
        break;
    }
    EXPECT_TRUE(found_logon_reply) << "did not find a Logon reply (35=A) in captured frames";
}

// ── (5) T005: micros → 52= is 24 chars ───────────────────────────────────────
//
// Session with sending_time_precision = micros → 52= must be 24 chars. [AC US1.2]
//
// RED: same as above — hardcoded millis → 21 chars → FAILS.
TEST_F(SendingTimePrecisionTest, SendingTimePrecision_Micros_Emits24Char52) {
    auto cfg = make_cfg(fixpp::core::fix_time_precision::micros);
    Session sess(engine, cfg);

    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";

    auto logon = make_peer_logon(1);
    auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut2.get().has_value()) << "peer Logon feed failed";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    ASSERT_FALSE(captured_frames.empty()) << "no frames emitted during open/logon";

    bool found_logon_reply = false;
    for (const auto& frame : captured_frames) {
        auto tag35 = extract_field(std::span<const std::byte>(frame), 35);
        if (!tag35 || *tag35 != "A") continue;
        found_logon_reply = true;

        auto tag52 = extract_field(std::span<const std::byte>(frame), 52);
        ASSERT_TRUE(tag52.has_value())
            << "Logon reply must contain tag 52 (SendingTime)";
        EXPECT_EQ(tag52->size(), 24u)
            << "micros config must emit 24-char SendingTime; got '" << *tag52 << "' (len="
            << tag52->size() << "); [AC US1.2]";
        break;
    }
    EXPECT_TRUE(found_logon_reply) << "did not find a Logon reply (35=A) in captured frames";
}

// ── (6) T005: default millis → 52= byte-identical to pre-feature ─────────────
//
// Unset config (default millis) → 52= must be the 21-char form, byte-identical
// to the pre-feature golden "20240101-00:00:00.123" (the mock clock returns
// 2024-01-01T00:00:00.123456789Z; millis truncates to .123).
// This witness must ALREADY PASS (unchanged path). [FR-003 / SC-002 / I-NST-1]
TEST_F(SendingTimePrecisionTest, SendingTimePrecision_DefaultMillis_ByteIdentical) {
    // Default cfg: sending_time_precision is millis (the default).
    auto cfg = make_cfg();  // no override → millis
    Session sess(engine, cfg);

    auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut.get().has_value()) << "open() failed";

    auto logon = make_peer_logon(1);
    auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut2.get().has_value()) << "peer Logon feed failed";
    ASSERT_EQ(sess.state(), fsm_state::Active);

    ASSERT_FALSE(captured_frames.empty()) << "no frames emitted during open/logon";

    bool found_logon_reply = false;
    for (const auto& frame : captured_frames) {
        auto tag35 = extract_field(std::span<const std::byte>(frame), 35);
        if (!tag35 || *tag35 != "A") continue;
        found_logon_reply = true;

        auto tag52 = extract_field(std::span<const std::byte>(frame), 52);
        ASSERT_TRUE(tag52.has_value())
            << "Logon reply must contain tag 52 (SendingTime)";

        // Exact byte-identity with the pre-feature millis output.
        // Mock clock → 2024-01-01T00:00:00.123456789Z → millis = .123
        EXPECT_EQ(tag52->size(), 21u)
            << "default millis must emit 21-char SendingTime; [FR-003 / SC-002 / I-NST-1]";
        EXPECT_EQ(*tag52, "20240101-00:00:00.123")
            << "default millis output must be byte-identical to pre-feature golden "
               "'20240101-00:00:00.123'; got '" << *tag52
            << "'; [FR-003 / SC-002 / I-NST-1]";
        break;
    }
    EXPECT_TRUE(found_logon_reply) << "did not find a Logon reply (35=A) in captured frames";
}

// ── T012 (US2): session inbound witnesses ─────────────────────────────────────
//
// These witnesses go in the same file and reuse the SendingTimePrecisionTest
// fixture (acceptor with mock clock, captured frames).
//
// Anchors: tasks T012; contracts C3/C7; data-model E3/I-NST-3/I-NST-4;
//          Gate A RC#1/New-4; quickstart 8/9/10.

// ── T012(4): InboundNanos52_ParsedNotRejected ────────────────────────────────
//
// Feed an inbound message whose 52= is a 27-char nanos timestamp.
// Post-T014: parsed + processed, NO malformed-field Reject (35=3 with RefTag=52).
// [SC-003; contract C3; data-model I-NST-3]
//
// RED pre-T014: the strict parser rejects length 27 → sending_time_ok=false →
// Reject(35=3, reason=10) + Logout + Disconnect emitted, session goes Disconnected.
// Post-T014: the lenient parser accepts length 27 → session stays Active.
TEST_F(SendingTimePrecisionTest, InboundNanos52_ParsedNotRejected) {
    auto cfg = make_cfg(fixpp::core::fix_time_precision::millis);
    Session sess(engine, cfg);

    // Drive to Active with a normal millis Logon.
    drive_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // Feed an inbound Heartbeat (35=0) with a 27-char nanos 52=.
    // seq=2 (Logon was seq=1). Mock clock UTC ≈ 2024-01-01T00:00:00.123Z, so
    // the nanos 52= is ~0 divergence from the clock → within MaxLatency (120 s default).
    const std::string_view nanos_ts = "20240101-00:00:00.123456789";
    ASSERT_EQ(nanos_ts.size(), 27u) << "sanity: nanos_ts must be 27 chars";

    auto hb_frame = make_fix44_frame_with_time("0", 2, "TW", "ISLD", nanos_ts);
    auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(hb_frame), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut.get().has_value()) << "on_inbound_frame returned error";

    // The session must remain Active (a parse failure → Reject + Logout + Disconnect).
    EXPECT_EQ(sess.state(), fsm_state::Active)
        << "session must stay Active after receiving a 27-char nanos 52=; "
           "[SC-003 / contract C3 / I-NST-3]";

    // No Reject (35=3) emitted with RefTagID=52 and reason=10.
    bool found_reject_52 = false;
    for (const auto& frame : captured_frames) {
        auto tag35 = extract_field(std::span<const std::byte>(frame), 35);
        if (!tag35 || *tag35 != "3") continue;
        std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
        if (wire.find("371=52\x01") != std::string::npos) {
            found_reject_52 = true;
            break;
        }
    }
    EXPECT_FALSE(found_reject_52)
        << "no Reject(35=3 RefTagID=52) must be emitted for a valid 27-char nanos 52=; "
           "[SC-003 / contract C3]";
}

// ── T012(5): MaxLatency_OnNanosInbound_BoundaryCorrect ───────────────────────
//
// Inbound ns 52= at the MaxLatency boundary → correct accept/reject.
// Asserts this needs NO new boundary logic — the existing check_sending_time
// arithmetic runs on the parsed instant. [FR-007/SC-004; Gate A New-4]
//
// Two sub-cases:
//   (a) Inbound nanos 52= within MaxLatency → session stays Active.
//   (b) Inbound nanos 52= beyond MaxLatency → Reject + Logout + Disconnect.
//
// RED pre-T014 for case (a): parser rejects length 27 → Disconnected even though
// the timestamp is valid. Post-T014: lenient parser accepts it.
TEST_F(SendingTimePrecisionTest, MaxLatency_OnNanosInbound_BoundaryCorrect) {
    // ── Sub-case (a): within MaxLatency → Active after inbound ───────────────
    {
        auto cfg = make_cfg(fixpp::core::fix_time_precision::millis);
        Session sess(engine, cfg);
        drive_to_active(sess);
        ASSERT_EQ(sess.state(), fsm_state::Active);

        // 52= at the mock clock time (divergence ≈ 0 ns < 120 s limit).
        const std::string_view ts_within = "20240101-00:00:00.123456789";
        auto hb = make_fix44_frame_with_time("0", 2, "TW", "ISLD", ts_within);
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(hb), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut.get().has_value()) << "on_inbound_frame(within) returned error";

        EXPECT_EQ(sess.state(), fsm_state::Active)
            << "session must stay Active: ns 52= within MaxLatency must be accepted; "
               "[FR-007/SC-004/New-4]";
    }

    // ── Sub-case (b): beyond MaxLatency → Disconnected ───────────────────────
    {
        ioc.restart();
        captured_frames.clear();

        auto cfg = make_cfg(fixpp::core::fix_time_precision::millis);
        cfg.sending_time_threshold = std::chrono::seconds{120};
        Session sess2(engine, cfg);
        drive_to_active(sess2);
        ASSERT_EQ(sess2.state(), fsm_state::Active);

        // 52= 121 s before the mock clock time → exceeds 120 s limit.
        const std::string_view ts_stale = "20231231-23:57:59.123456789";
        auto hb = make_fix44_frame_with_time("0", 2, "TW", "ISLD", ts_stale);
        auto fut = asio::co_spawn(ioc, sess2.on_inbound_frame(hb), asio::use_future);
        run_ioc();
        (void)fut.get();

        EXPECT_EQ(sess2.state(), fsm_state::Disconnected)
            << "session must Disconnect: ns 52= beyond MaxLatency (121s > 120s); "
               "[FR-007/SC-004/New-4]";
    }
}

// ── T012(6): OrigSendingTime122_PreservedVerbatim_OnResend ───────────────────
//
// On a PossDup resend, OrigSendingTime(122) must echo the STORED ORIGINAL 52=
// bytes verbatim and must NOT be re-stamped at the configured precision.
// [FR-006/I-NST-4; Gate A RC#1]
//
// Design (non-vacuous, distinguishing oracle):
//
//   1. Configure nanos precision + CapturingStoreFactory (required for resend
//      to retrieve stored frames via build_replay_frame).
//   2. Drive to Active. The Logon reply (admin, seq=1) is stored but will be
//      GapFilled by the resend path (admin not replayed per FIX-SL §4.3.5).
//   3. Send an APP message (35=D) via Session::send(). It is stored at seq=2
//      and emitted with 52= stamped at nanos precision by the current clock
//      (= "20240101-00:00:00.123456789"). Capture that 52= as "original_52".
//   4. Advance the mock clock by 10 s. A resend-time re-stamp would produce
//      a DIFFERENT value ("20240101-00:00:10.123456789") — distinguishable from
//      the original. This makes accidental re-stamping detectable.
//   5. Feed a ResendRequest(7=2, 16=2) from the peer.
//   6. build_replay_frame fires for the app message → emits 43=Y + 122=.
//   7. ASSERT found_replay_frame (no SUCCEED() escape).
//   8. EXPECT 122= == original_52 (the stored bytes, NOT the advanced-clock value).
//
// Distinguishing: if 122 were re-stamped at resend time, it would be
// "20240101-00:00:10.123456789" (10 s later), failing the EXPECT.
// [FR-006 / I-NST-4 / Gate A RC#1; quickstart 10; contract C3/C7]
TEST_F(SendingTimePrecisionTest, OrigSendingTime122_PreservedVerbatim_OnResend) {
    // Configure nanos precision + real store so build_replay_frame fires.
    auto store_factory = std::make_shared<CapturingStoreFactory126>();
    auto cfg = make_cfg(fixpp::core::fix_time_precision::nanos, store_factory);
    Session sess(engine, cfg);

    // Drive to Active. The Logon reply (seq=1, admin) is stored but will be
    // GapFilled on ResendRequest (admin not replayed — FIX-SL §4.3.5).
    // drive_to_active() clears captured_frames after reaching Active.
    drive_to_active(sess);
    ASSERT_EQ(sess.state(), fsm_state::Active);

    // ── Step 3: send an app message at clock T0 ──────────────────────────────
    // Mock clock UTC at T0: 2024-01-01T00:00:00.123456789Z (from SetUp).
    // The nanos stamp will be "20240101-00:00:00.123456789".
    const char kAppPayload[] = "35=D\x01" "11=ORD001\x01" "54=1\x01" "55=AAPL\x01";
    std::vector<std::byte> payload;
    for (char c : std::string_view{kAppPayload, sizeof(kAppPayload) - 1})
        payload.push_back(static_cast<std::byte>(c));

    auto fut_send = asio::co_spawn(
        ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut_send.get().has_value()) << "Session::send() failed for app message";
    ASSERT_FALSE(captured_frames.empty()) << "app message must have been emitted";

    // Capture the 52= from the emitted app frame (the last captured frame).
    const auto& app_frame = captured_frames.back();
    auto tag52_opt = extract_field(std::span<const std::byte>(app_frame), 52);
    ASSERT_TRUE(tag52_opt.has_value()) << "app frame must contain tag 52 (SendingTime)";
    const std::string original_52 = std::string(*tag52_opt);
    // The original_52 is the nanos-stamped value at T0.
    ASSERT_EQ(original_52.size(), 27u)
        << "nanos config must emit 27-char 52=; got '" << original_52 << "'";

    // Capture the seqnum of the app message (tag 34) so we can request it.
    auto tag34_opt = extract_field(std::span<const std::byte>(app_frame), 34);
    ASSERT_TRUE(tag34_opt.has_value()) << "app frame must contain tag 34 (MsgSeqNum)";
    const seqnum_t app_seq = static_cast<seqnum_t>(std::stoul(std::string(*tag34_opt)));

    captured_frames.clear();  // discard; we only care about the replay frame next.

    // ── Step 4: advance the clock by 10 s ───────────────────────────────────
    // After advance, clock UTC = 2024-01-01T00:00:10.123456789Z.
    // A re-stamp at resend time would produce "20240101-00:00:10.123456789".
    // This is DIFFERENT from original_52 ("20240101-00:00:00.123456789") — the
    // two differ at the seconds digit (":00" vs ":10").
    clock->advance(std::chrono::seconds{10});

    // Verify the distinguishing postcondition: the advanced-clock stamp would be
    // a different value. We compute it directly to make the reasoning explicit.
    {
        std::array<char, 32> stamp_buf{};
        using namespace std::chrono;
        const auto advanced_utc = clock->now();
        auto stamp_r = fixpp::core::utc_time_to_fix_string(
            advanced_utc, fixpp::core::fix_time_precision::nanos,
            std::span<char>{stamp_buf});
        ASSERT_TRUE(stamp_r.has_value());
        const std::string advanced_stamp(stamp_r->data(), stamp_r->size());
        ASSERT_NE(advanced_stamp, original_52)
            << "sanity: advanced-clock stamp must differ from original_52 "
               "(otherwise the test cannot distinguish re-stamp from byte-copy)";
    }

    // ── Step 5: feed ResendRequest for the app message ───────────────────────
    // Peer inbound seqnum is 2 (Logon was seq=1; ResendRequest is next).
    auto rr = make_resend_request_44(2, "TW", "ISLD", app_seq, app_seq);
    auto fut_rr = asio::co_spawn(ioc, sess.on_inbound_frame(rr), asio::use_future);
    run_ioc();
    ASSERT_TRUE(fut_rr.get().has_value()) << "ResendRequest feed failed";

    // ── Steps 6–8: assert the replayed frame carries 122= == original_52 ────
    // build_replay_frame fires for the app message (35=D is NOT admin).
    // It byte-copies the stored 52= into 122= — the stored value is original_52.
    // A re-stamp at resend time (wrong behavior) would produce a different value.
    bool found_replay_frame = false;
    for (const auto& frame : captured_frames) {
        std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
        // build_replay_frame always adds 43=Y on the replayed app message.
        if (wire.find("43=Y\x01") == std::string::npos) continue;
        found_replay_frame = true;

        auto tag122 = extract_field(std::span<const std::byte>(frame), 122);
        ASSERT_TRUE(tag122.has_value())
            << "replayed frame with 43=Y must carry 122= (OrigSendingTime); [I-NST-4]";
        EXPECT_EQ(*tag122, original_52)
            << "122= must be the STORED ORIGINAL 52= bytes (NOT re-stamped at the "
               "advanced-clock time); stored='" << original_52
            << "' got='" << *tag122 << "'; [FR-006 / I-NST-4 / Gate A RC#1]";
        break;
    }
    // No SUCCEED() escape: the test MUST find a replayed 43=Y frame.
    // build_replay_frame fires for app messages (35=D is not admin).
    ASSERT_TRUE(found_replay_frame)
        << "ResendRequest for a stored app message (35=D, seq=" << app_seq << ") must "
           "produce a replayed frame with 43=Y + 122=; [build_replay_frame / I-NST-4]";
}

}  // namespace fixpp::session::test

// ── T017 [P]: SendingTimePrecision_NoHeapOnFormatParse ────────────────────────
//
// No-heap witness: assert zero global-heap allocation across
// utc_time_to_fix_string + fix_string_to_utc_time at every precision
// (seconds/millis/micros/nanos) and across all lenient-parse widths 1–9.
//
// The BINDING gate is the mallocnesia LD_PRELOAD interceptor — NOT just a
// counting_pmr_resource (a tracking-PMR guard is a false-pass per
// [[feedback_tracking_pmr_resource_false_pass]]: non-PMR allocs escape via
// global new, not through the PMR). Under mallocnesia, any call to global
// malloc/free between alloc_guard_start() and alloc_guard_end() increments the
// counter and causes alloc_guard_count() to return nonzero.
//
// The witness body calls format + parse in a tight loop for every precision
// and every fraction width. Under mallocnesia, a nonzero count aborts the
// process. Without the LD_PRELOAD, the weak stubs are no-ops and the test
// passes (the binding proof is the _mallocnesia ctest variant registered in
// tests/session/CMakeLists.txt).
//
// Anchors: tasks T017; quickstart 11; plan §VIII.5; contract I-NST-5;
//          [[feedback_tracking_pmr_resource_false_pass]].
//
// Note: this test is at file scope (not inside fixpp::session::test) so that
// the ::fixpp::core qualified names are unambiguous.

// mallocnesia weak-symbol hooks — replaced by LD_PRELOAD; no-ops otherwise.
extern "C" {
// NOLINTNEXTLINE(misc-use-anonymous-namespace) — must be at file scope for LD_PRELOAD override.
__attribute__((weak)) void alloc_guard_start() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) void alloc_guard_end() {}
// NOLINTNEXTLINE(misc-use-anonymous-namespace)
__attribute__((weak)) long alloc_guard_count() { return 0; }
}

TEST(SendingTimePrecisionNoHeap, SendingTimePrecision_NoHeapOnFormatParse) {
    using namespace std::chrono;
    using ::fixpp::core::fix_time_precision;
    using ::fixpp::core::utc_time_to_fix_string;
    using ::fixpp::core::fix_string_to_utc_time;

    // A concrete time point with a non-zero nanosecond remainder so the
    // nanos path exercises the full 9-digit branch.
    // 2024-01-01T12:34:56.123456789Z
    const auto tp = system_clock::time_point{seconds{1704109696}} + nanoseconds{123456789};

    // Buffer large enough for all precisions (nanos = 27 chars; 32 is safe).
    std::array<char, 32> buf{};

    constexpr fix_time_precision precisions[] = {
        fix_time_precision::seconds,
        fix_time_precision::millis,
        fix_time_precision::micros,
        fix_time_precision::nanos,
    };

    // ── Start the global-heap interception window ─────────────────────────────
    alloc_guard_start();

    // Format at every precision in a tight loop.
    for (auto prec : precisions) {
        auto result = utc_time_to_fix_string(tp, prec, std::span<char>{buf});
        // Post-condition: the format must succeed and return a non-empty span.
        // Under mallocnesia, a heap alloc here aborts via the interceptor.
        ASSERT_TRUE(result.has_value())
            << "utc_time_to_fix_string must succeed at precision "
            << static_cast<int>(prec);
        ASSERT_FALSE(result->empty()) << "formatted span must not be empty";
    }

    // Parse the nanos form (27 chars) — exercises the lenient-parse path.
    {
        auto nanos_result = utc_time_to_fix_string(
            tp, fix_time_precision::nanos, std::span<char>{buf});
        ASSERT_TRUE(nanos_result.has_value());
        const auto as_bytes_span = std::span<const char>{nanos_result->data(), nanos_result->size()};
        auto parse_result = fix_string_to_utc_time(as_bytes_span);
        ASSERT_TRUE(parse_result.has_value())
            << "fix_string_to_utc_time must accept the nanos form (27 chars)";
    }

    // Parse across all lenient widths 1–9 (each total length = 18+N):
    //   base "20240101-12:34:56" (17 chars) + "." + N digits
    // Build each test string on the stack (no std::string heap).
    {
        // Use the seconds base from the nanos format result (first 17 chars).
        auto secs_result = utc_time_to_fix_string(
            tp, fix_time_precision::seconds, std::span<char>{buf});
        ASSERT_TRUE(secs_result.has_value());
        ASSERT_EQ(secs_result->size(), 17u);

        // Stack buffer for the width-N test strings (max = 17 + 1 + 9 = 27 chars).
        std::array<char, 28> wbuf{};
        // Copy the 17-char base.
        for (std::size_t i = 0; i < 17; ++i) wbuf[i] = (*secs_result)[i];
        wbuf[17] = '.';

        for (int n = 1; n <= 9; ++n) {
            // Fill n ASCII digit characters after the dot.
            for (int d = 0; d < n; ++d) wbuf[18 + d] = static_cast<char>('1' + (d % 9));
            const auto span = std::span<const char>{wbuf.data(), static_cast<std::size_t>(18 + n)};
            auto r = fix_string_to_utc_time(span);
            ASSERT_TRUE(r.has_value())
                << "fix_string_to_utc_time must accept width-" << n
                << " fraction (lenient parse, FR-004/SC-003)";
        }
    }

    // Also parse the bare 17-char (seconds, no dot) form.
    {
        auto secs_result = utc_time_to_fix_string(
            tp, fix_time_precision::seconds, std::span<char>{buf});
        ASSERT_TRUE(secs_result.has_value());
        auto r = fix_string_to_utc_time(std::span<const char>{secs_result->data(), secs_result->size()});
        ASSERT_TRUE(r.has_value())
            << "fix_string_to_utc_time must accept bare 17-char seconds form";
    }

    // ── End the global-heap interception window ───────────────────────────────
    alloc_guard_end();

    // Under mallocnesia: alloc_guard_count() returns the number of global
    // malloc/free calls in the window. A nonzero count means the code touched
    // the heap and the no-heap contract is violated.
    const long heap_allocs = alloc_guard_count();
    EXPECT_EQ(heap_allocs, 0L)
        << "utc_time_to_fix_string + fix_string_to_utc_time must not touch "
           "the global heap at any precision or parse width; "
           "heap_allocs=" << heap_allocs
        << "; [plan §VIII.5 / I-NST-5 / [[feedback_tracking_pmr_resource_false_pass]]]";
}
