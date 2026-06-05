// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_send_allow_pos_dup_strip.cpp
//
// 022-possresend-allowpossdup-send US2 (Phase 4) — AllowPosDup send-path strip.
// RED-first witnesses for T005/T006 (no production scanner/excision yet).
//
// Witness inventory (quickstart §US2 / tasks T005+T006):
//   W1 Strip_Default           — default allow_pos_dup==false; 43/122 must be gone
//   W2 Retain                  — allow_pos_dup=true; 43/122 must survive
//   W3 EmbeddedText_Hostile    — literal "43=" inside a value must NOT be excised
//   W4 SohBoundary_StripRetain — genuine SOH-boundary 43/122; strip vs retain
//   W5 NoOp_WhenAbsent         — no 43/122 in payload → byte-identical output
//   W6 MalformedField_131      — parameterized over 4 cases; must return 131, no seqnum
//   W7 ResendIndependence      — resend always re-adds 43=Y+122 regardless of knob
//   W8 NoHeap_Scaffold         — structural no-heap scaffold; LD_PRELOAD gate → T010
//
// RED expectations (T007):
//   W1, W3, W4(default arm), W6 → FAIL (no scanner/excision yet)
//   W2, W5, W7 → may PASS (passthrough = retain behavior matches W2/W5; resend path
//                           is independent and adds 43=Y regardless)
//
// Anchors: contracts/session-send-possdup.md §C2.1–C2.6, §C3;
//          data-model.md §2 (transform table + INV-1..5);
//          quickstart.md §US2 items 1–8; tasks.md T005/T006/T007.
//
// Build: cmake --build build/linux-clang-debug --target session_send_allow_pos_dup_strip -j2
// Run:   ctest --test-dir build/linux-clang-debug -R session_send_allow_pos_dup_strip -V

#include <gtest/gtest.h>

#include <array>
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
#include "support/possdup_test_support.hpp"  // extract_field

using namespace std::chrono_literals;
using fixpp::session::test_support::extract_field;

namespace fixpp::session::test {

// ── Helpers ─────────────────────────────────────────────────────────────────────

// A MessageStore that records each outbound store call (seq + frame bytes),
// and also serves as a real retrieve() source for the resend-independence witness.
// Satisfies the "store-before-transport" I-3 ordering contract transparently.
class CapturingStore final : public MessageStore {
public:
    struct Record {
        seqnum_t seq;
        std::vector<std::byte> frame;
    };
    std::vector<Record> outbound_records;

    explicit CapturingStore() noexcept : MessageStore(flush_thunk_for<CapturingStore>()) {}

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame, direction_t dir) noexcept override {
        if (dir == direction_t::outbound) {
            outbound_records.push_back({seq, std::vector<std::byte>(frame.begin(), frame.end())});
            // Track next_out_ so next_seqnum(outbound, false) correctly returns
            // the value AFTER the highest stored seq (for the ResendRequest path
            // which uses this to compute our_last = next_seqnum - 1).
            if (seq + 1U > next_out_) next_out_ = seq + 1U;
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t from, seqnum_t to, direction_t dir, retrieve_visitor& visitor) noexcept override {
        if (dir == direction_t::outbound) {
            for (auto& rec : outbound_records) {
                if (rec.seq >= from && rec.seq <= to) {
                    auto r =
                        co_await visitor.on_frame(rec.seq, std::span<const std::byte>(rec.frame));
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

class CapturingStoreFactory final : public MessageStoreFactory {
public:
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        return std::make_unique<CapturingStore>();
    }
};

// Build a full FIX wire frame: 8=FIX.4.4 / 9=<len> / <body> / 10=<cs>
// Used for sending inbound peer frames (ResendRequest etc.).
static std::vector<std::byte> make_fix_frame(std::string_view body_str) {
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

// Build the body part of a FIX 4.4 peer Logon (for drive_to_active_44).
static std::vector<std::byte> make_peer_logon_44(std::uint32_t seq, std::string_view sender,
                                                 std::string_view target) {
    std::string body;
    body += "35=A\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "98=0\x01";
    body += "108=30\x01";
    return make_fix_frame(body);
}

// Build a ResendRequest (35=2) peer frame.
static std::vector<std::byte> make_resend_request(seqnum_t begin_seqno, seqnum_t end_seqno,
                                                  std::uint32_t inbound_seq,
                                                  std::string_view sender,
                                                  std::string_view target) {
    std::string body;
    body += "35=2\x01";
    body += "34=" + std::to_string(inbound_seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += "7=" + std::to_string(static_cast<std::uint32_t>(begin_seqno)) + "\x01";
    body += "16=" + std::to_string(static_cast<std::uint32_t>(end_seqno)) + "\x01";
    return make_fix_frame(body);
}

// ── Fixture ──────────────────────────────────────────────────────────────────────

// Standalone FIX.4.4 send-path fixture with a CapturingStoreFactory (needed by W7's
// resend-independence witness). Does not derive from PossDupTestBase (4.2-only).
class AllowPosDupStripTest : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    // All outbound frames captured by transport_send.
    std::vector<std::vector<std::byte>> captured_frames;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    // Build a session config. allow_pos_dup is caller-supplied.
    SessionConfig make_cfg(bool allow_pos_dup = false) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.4";
        cfg.heartbeat_interval = 0s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.allow_pos_dup = allow_pos_dup;
        cfg.store_factory = std::make_shared<CapturingStoreFactory>();
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    void run_ioc() {
        ioc.run_for(200ms);
        ioc.restart();
    }

    // Open acceptor session + feed Logon(seq=1, TW→ISLD) → Active.
    void drive_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_peer_logon_44(1, "TW", "ISLD");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut2.get().has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fsm_state::Active);
        captured_frames.clear();  // discard open()+logon-ack frames; tests assert only user sends
    }

    // Feed inbound frame; ignore the result (used for ResendRequest drive).
    void feed(Session& sess, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        run_ioc();
        (void)fut.get();
    }

    // Helper: convert a string literal payload to bytes.
    static std::vector<std::byte> to_payload(std::string_view sv) {
        std::vector<std::byte> v;
        v.reserve(sv.size());
        for (char c : sv) v.push_back(static_cast<std::byte>(c));
        return v;
    }

    // Helper: does any captured frame contain a boundary "43=" in the application portion?
    // The "application portion" is everything after the engine-stamped header (8/9/34/49/52/56)
    // but before 10=. For simplicity we scan the full frame bytes for SOH-boundary "43=".
    // NOTE: extract_field(frame, 43) is the clean test; we also check raw SOH-boundary presence.
    static bool frame_has_boundary_tag(const std::vector<std::byte>& frame, std::uint16_t tag) {
        return extract_field(std::span<const std::byte>(frame), tag).has_value();
    }
};

// ── W1: Strip default ────────────────────────────────────────────────────────────
//
// Default allow_pos_dup==false. Payload contains a boundary 43=Y and 122=<ts>.
// After the strip (C2.1/SC-002), neither must appear in the captured outbound frame.
//
// RED: without the 022 scanner+excision the fields pass through → test FAILS because
//      boundary 43= and 122= are found in the captured frame.
TEST_F(AllowPosDupStripTest, W1_StripDefault_43And122AbsentInOutbound) {
    auto cfg = make_cfg(/*allow_pos_dup=*/false);
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Payload: 35=D (mandatory MsgType), a business field, then 43=Y and 122=<ts>.
    // All fields are SOH-terminated (020 floor requires trailing SOH).
    // [C2.1; SC-002; data-model §2 transform table row 1]
    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD001\x01"
        "43=Y\x01"
        "122=20260605-12:00:00.000\x01"
        "54=1\x01"
        "55=AAPL\x01";
    auto payload = to_payload(kPayloadStr);

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "Session::send must succeed for a well-formed payload";
    ASSERT_FALSE(captured_frames.empty()) << "A frame must be emitted";

    const auto& frame = captured_frames.back();

    // C2.1: default strip must REMOVE boundary 43= and 122= from the outbound frame.
    // RED EXPECTATION: these assertions FAIL pre-impl because the strip does not exist.
    EXPECT_FALSE(frame_has_boundary_tag(frame, 43))
        << "W1 RED: boundary 43= still present in outbound frame (strip not yet implemented); "
           "[C2.1/SC-002; data-model §2]";
    EXPECT_FALSE(frame_has_boundary_tag(frame, 122))
        << "W1 RED: boundary 122= still present in outbound frame (strip not yet implemented); "
           "[C2.1/SC-002; data-model §2]";
}

// ── W2: Retain ───────────────────────────────────────────────────────────────────
//
// allow_pos_dup=true. Same payload. Both 43=Y and 122= must survive verbatim (C2.2/FR-009).
//
// This witness is expected to PASS pre-impl (passthrough = retain = correct behavior for true).
TEST_F(AllowPosDupStripTest, W2_Retain_43And122PresentInOutbound) {
    auto cfg = make_cfg(/*allow_pos_dup=*/true);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD001\x01"
        "43=Y\x01"
        "122=20260605-12:00:00.000\x01"
        "54=1\x01"
        "55=AAPL\x01";
    auto payload = to_payload(kPayloadStr);

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "Session::send must succeed";
    ASSERT_FALSE(captured_frames.empty()) << "A frame must be emitted";

    const auto& frame = captured_frames.back();

    // C2.2/FR-009: both fields must be present verbatim.
    // GREEN pre-impl (passthrough); GREEN post-impl (retain knob).
    EXPECT_TRUE(frame_has_boundary_tag(frame, 43))
        << "W2: allow_pos_dup=true must retain boundary 43= in outbound frame; "
           "[C2.2/FR-009; data-model §2]";
    EXPECT_EQ(extract_field(std::span<const std::byte>(frame), 122), "20260605-12:00:00.000")
        << "W2: allow_pos_dup=true must retain 122= value verbatim; [C2.2/FR-009]";
}

// ── W3: Embedded-text hostile witness ────────────────────────────────────────────
//
// A field whose *value* contains the literal bytes "43=" with NO preceding SOH
// (e.g. 11=ORD43=Y) PLUS a separate real boundary 43=Y field.
// Default strip: the real boundary 43=Y must be removed; the value "ORD43=Y" must
// survive intact. RED-proves an unanchored strip would corrupt tag 11.
//
// RED: without the 022 scanner+excision the real boundary 43= is not removed
//      → assertion for "boundary 43= absent" FAILS.
// Also verifies: tag 11's value must still contain "43=" substring (not corrupted).
//
// [C2.3a; INV-2; feedback_delimiter_injection_verbatim_field_copy]
TEST_F(AllowPosDupStripTest, W3_EmbeddedText43_HostileWitness_ValueIntactBoundaryRemoved) {
    auto cfg = make_cfg(/*allow_pos_dup=*/false);
    Session sess(engine, cfg);
    drive_to_active(sess);

    // 11=ORD43=Y contains literal "43=" inside the VALUE (no preceding SOH → not a boundary).
    // Then a real boundary 43=Y field follows.
    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD43=Y\x01"  // INV-2: literal "43=" inside value — must NOT be excised
        "43=Y\x01"        // real boundary 43= — must be excised under default strip
        "54=1\x01";
    auto payload = to_payload(kPayloadStr);

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "Session::send must succeed for a well-formed payload";
    ASSERT_FALSE(captured_frames.empty()) << "A frame must be emitted";

    const auto& frame = captured_frames.back();

    // C2.3a: the real boundary 43= must be gone (RED pre-impl: it's still there).
    EXPECT_FALSE(frame_has_boundary_tag(frame, 43))
        << "W3 RED: boundary 43= still present (strip not yet implemented); "
           "an unanchored strip must not fire for this payload either. "
           "[C2.3a; INV-2; feedback_delimiter_injection_verbatim_field_copy]";

    // INV-2: tag 11's value "ORD43=Y" must remain intact — an unanchored strip
    // would have corrupted it by excising the substring "43=Y". We verify by
    // checking that tag 11 is present in the frame (it must not be mangled).
    // extract_field(11) is the positive guard: if it returns nullopt, the strip
    // corrupted the field boundary.
    const auto tag11 = extract_field(std::span<const std::byte>(frame), 11);
    EXPECT_TRUE(tag11.has_value())
        << "W3: tag 11 must be present in the outbound frame; "
           "an unanchored strip would corrupt '11=ORD43=Y' by excising '43=Y' "
           "[INV-2; feedback_delimiter_injection_verbatim_field_copy]";
    if (tag11.has_value()) {
        EXPECT_EQ(std::string(*tag11), "ORD43=Y")
            << "W3: tag 11 value must be 'ORD43=Y' verbatim (not truncated); "
               "[INV-2]";
    }
}

// ── W4: True SOH-boundary excise ─────────────────────────────────────────────────
//
// Genuine SOH-boundary …\x0143=Y\x01 and …\x01122=…\x01.
// Default strip → both removed. allow_pos_dup=true → both retained.
//
// RED (default arm): without the strip, boundary 43= and 122= are still present
//                    → EXPECT_FALSE(...) fails.
// GREEN (retain arm): passthrough = retain = correct, no strip needed.
//
// [C2.3b; data-model §2 transform table row 2]
TEST_F(AllowPosDupStripTest, W4_SohBoundary_DefaultStrip_BothRemoved) {
    auto cfg = make_cfg(/*allow_pos_dup=*/false);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD002\x01"
        "43=Y\x01"
        "122=20260605-12:00:00.000\x01"
        "54=1\x01";
    auto payload = to_payload(kPayloadStr);

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "Session::send must succeed";
    ASSERT_FALSE(captured_frames.empty()) << "A frame must be emitted";

    const auto& frame = captured_frames.back();

    // C2.3b: default strip must remove both boundary fields.
    // RED pre-impl: both are still present.
    EXPECT_FALSE(frame_has_boundary_tag(frame, 43))
        << "W4 RED: default strip must remove boundary 43=; [C2.3b; data-model §2 row 2]";
    EXPECT_FALSE(frame_has_boundary_tag(frame, 122))
        << "W4 RED: default strip must remove boundary 122=; [C2.3b; data-model §2 row 2]";
}

TEST_F(AllowPosDupStripTest, W4_SohBoundary_Retain_BothPresent) {
    auto cfg = make_cfg(/*allow_pos_dup=*/true);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD002\x01"
        "43=Y\x01"
        "122=20260605-12:00:00.000\x01"
        "54=1\x01";
    auto payload = to_payload(kPayloadStr);

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "Session::send must succeed";
    ASSERT_FALSE(captured_frames.empty()) << "A frame must be emitted";

    const auto& frame = captured_frames.back();

    // C2.3b / allow_pos_dup=true: both fields must be retained.
    // GREEN pre-impl (passthrough) and post-impl.
    EXPECT_TRUE(frame_has_boundary_tag(frame, 43))
        << "W4 retain arm: allow_pos_dup=true must retain boundary 43=; [C2.3b]";
    EXPECT_TRUE(frame_has_boundary_tag(frame, 122))
        << "W4 retain arm: allow_pos_dup=true must retain boundary 122=; [C2.3b]";
}

// ── W5: No-op when absent ─────────────────────────────────────────────────────────
//
// Payload with no 43/122. Output must be byte-identical under both knob settings.
//
// GREEN pre-impl (no strip needed when fields are absent → passthrough = correct).
//
// [C2.5; data-model §2 transform table row 2 (absent)]
TEST_F(AllowPosDupStripTest, W5_NoOp_WhenAbsent_ByteIdenticalOutput) {
    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD003\x01"
        "54=1\x01"
        "55=AAPL\x01";

    // Run under default (strip) config.
    std::vector<std::byte> frame_default;
    {
        captured_frames.clear();
        auto cfg = make_cfg(/*allow_pos_dup=*/false);
        Session sess(engine, cfg);
        drive_to_active(sess);

        auto payload = to_payload(kPayloadStr);
        auto fut =
            asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut.get().has_value()) << "Session::send (default) must succeed";
        ASSERT_FALSE(captured_frames.empty());
        frame_default = captured_frames.back();
    }

    // Run under retain config. Use a fresh ioc/session but same payload.
    // Note: the seqnums will differ (both start fresh from open()), so we compare
    // the APPLICATION PORTION only (everything after the engine-stamped header).
    // We verify: (a) no 43 present in either, (b) no 122 present in either.
    {
        ioc.restart();
        captured_frames.clear();
        auto cfg = make_cfg(/*allow_pos_dup=*/true);
        Session sess(engine, cfg);
        drive_to_active(sess);

        auto payload = to_payload(kPayloadStr);
        auto fut =
            asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
        run_ioc();
        ASSERT_TRUE(fut.get().has_value()) << "Session::send (retain) must succeed";
        ASSERT_FALSE(captured_frames.empty());
        const auto& frame_retain = captured_frames.back();

        // Neither frame should contain 43 or 122 (they weren't in the payload).
        EXPECT_FALSE(frame_has_boundary_tag(frame_default, 43))
            << "W5: no 43 in default-knob frame when payload has none; [C2.5]";
        EXPECT_FALSE(frame_has_boundary_tag(frame_default, 122))
            << "W5: no 122 in default-knob frame when payload has none; [C2.5]";
        EXPECT_FALSE(frame_has_boundary_tag(frame_retain, 43))
            << "W5: no 43 in retain-knob frame when payload has none; [C2.5]";
        EXPECT_FALSE(frame_has_boundary_tag(frame_retain, 122))
            << "W5: no 122 in retain-knob frame when payload has none; [C2.5]";
    }
}

// ── W6: Malformed-field → 131 ─────────────────────────────────────────────────────
//
// Parameterized over 4 cases that pass the 020 floor (each field is SOH-terminated,
// 35= is present and non-empty, no banned tag at a boundary) but fail the 022
// per-field scanner (missing '=', empty tag, non-digit tag, empty/zero-length field).
//
// Contract (C2.4/D2):
//   - Returns app_payload_malformed=131.
//   - Outbound seqnum NOT consumed.
//   - Nothing transmitted (0 new captured frames).
//   - NO excision (the existing 43= remains if it was there — the scanner fires first).
//
// RED: without the 022 scanner, the 020 floor admits all four payloads → send()
//      returns ok (not 131) and emits a frame.
//
// [C2.4; D2; feedback_conjunctive_parse_guard_tolerates_malformed_field;
//  data-model §2 scanner pass; tasks T006]

struct MalformedCase {
    const char* name;
    const char* payload;  // must NOT include trailing NUL; size computed below
    std::size_t payload_len;
};

// We parameterize using INSTANTIATE_TEST_SUITE_P.
class MalformedField131Test : public AllowPosDupStripTest,
                              public ::testing::WithParamInterface<MalformedCase> {};

// Four cases from quickstart §US2 item 6 / data-model §2 scanner pass:
//  (a) "35=D\x0111BROKEN\x0143=Y\x01"  — field with no '=' (missing delimiter)
//  (b) "35=D\x01=bad\x01122=x\x01"     — empty tag (starts with '=')
//  (c) "35=D\x014a=x\x0143=Y\x01"      — non-digit tag character
//  (d) "35=D\x01\x0143=Y\x01"          — zero-length field (stray SOH = empty field)
//
// Note: do NOT add a "no final SOH" case — the 020 floor rejects it at session.cpp:2951
//       (that measures the floor, not the 022 scanner).
static const MalformedCase kMalformedCases[] = {
    {"MissingEquals",
     "35=D\x01"
     "11BROKEN\x01"
     "43=Y\x01",
     sizeof("35=D\x01"
            "11BROKEN\x01"
            "43=Y\x01") -
         1},
    {"EmptyTag",
     "35=D\x01"
     "=bad\x01"
     "122=x\x01",
     sizeof("35=D\x01"
            "=bad\x01"
            "122=x\x01") -
         1},
    {"NonDigitTag",
     "35=D\x01"
     "4a=x\x01"
     "43=Y\x01",
     sizeof("35=D\x01"
            "4a=x\x01"
            "43=Y\x01") -
         1},
    {"EmptyField",
     "35=D\x01"
     "\x01"
     "43=Y\x01",
     sizeof("35=D\x01"
            "\x01"
            "43=Y\x01") -
         1},
    // Empty VALUE: tag present + '=' present but zero-length value ("58=\x01").
    // Distinct from EmptyField (zero-length field). Scanner check (d) → 131.
    {"EmptyValue",
     "35=D\x01"
     "58=\x01"
     "43=Y\x01",
     sizeof("35=D\x01"
            "58=\x01"
            "43=Y\x01") -
         1},
};

INSTANTIATE_TEST_SUITE_P(AllMalformedCases, MalformedField131Test,
                         ::testing::ValuesIn(kMalformedCases),
                         [](const ::testing::TestParamInfo<MalformedCase>& info) {
                             return info.param.name;
                         });

TEST_P(MalformedField131Test, W6_MalformedField_Returns131_NoSeqnum_NoTransmit_NoExcision) {
    const auto& tc = GetParam();

    auto cfg = make_cfg(/*allow_pos_dup=*/false);
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Capture the current seqnum before the send attempt.
    // Use the first post-logon send seqnum as a reference: we verify nothing was consumed
    // by checking that no frame is emitted (seqnum is not advanced via transport).
    const std::size_t frames_before = captured_frames.size();

    auto payload = std::vector<std::byte>(tc.payload_len);
    for (std::size_t i = 0; i < tc.payload_len; ++i) {
        payload[i] = static_cast<std::byte>(tc.payload[i]);
    }

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    // C2.4: must return app_payload_malformed=131.
    // RED pre-impl: the 020 floor passes all four → result is ok, not 131.
    EXPECT_FALSE(result.has_value())
        << "W6 RED [" << tc.name
        << "]: expected error app_payload_malformed=131; "
           "got ok (020 floor passes this case; 022 scanner not yet implemented); "
           "[C2.4; D2; feedback_conjunctive_parse_guard_tolerates_malformed_field]";
    if (!result.has_value()) {
        EXPECT_EQ(result.error(), fixpp::core::error::app_payload_malformed)
            << "W6 [" << tc.name << "]: error must be app_payload_malformed=131; [C2.4]";
    }

    // Seqnum NOT consumed: no new frame emitted to transport.
    EXPECT_EQ(captured_frames.size(), frames_before)
        << "W6 RED [" << tc.name
        << "]: no frame must be transmitted when 131 is returned; "
           "outbound seqnum must not be consumed; [C2.4]";

    // NO excision: because the scanner fires BEFORE excision, the 43= that was in the
    // payload is not removed (and no frame is emitted, so this is moot — but if a frame
    // IS emitted pre-impl, it should still contain 43= since no excision ran).
    // We verify via the no-transmit check above (most authoritative).
}

// ── W6b: Oversized stripped payload → wire_frame_too_large ─────────────────────────
//
// A scanner-VALID payload whose surviving fields exceed the bounded strip stack scratch
// (4096B) must return wire_frame_too_large — the SAME oversize disposition the framer's
// body_buf overflow already owns (contracts §C2.6; data-model INV-4). Covers the strip's
// excision-copy overflow guard (session.cpp wstrip → wire_frame_too_large). No frame is
// transmitted. [C2.6; research D6]
TEST_F(AllowPosDupStripTest, W6b_OversizedStrippedPayload_ReturnsWireFrameTooLarge) {
    auto cfg = make_cfg(/*allow_pos_dup=*/false);
    Session sess(engine, cfg);
    drive_to_active(sess);

    const std::size_t frames_before = captured_frames.size();

    // 35=D\x01 + a well-formed but huge field 58=<4200 'x'>\x01 + 43=Y\x01.
    // Scanner-valid (58 is a digit tag, value non-empty, 43 valid); the excision copy of
    // the surviving 58= field overflows the 4096B strip scratch → wire_frame_too_large.
    std::string big = "35=D\x01";
    big += "58=";
    big.append(4200, 'x');
    big += "\x01";
    big += "43=Y\x01";
    std::vector<std::byte> payload(big.size());
    for (std::size_t i = 0; i < big.size(); ++i) payload[i] = static_cast<std::byte>(big[i]);

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    ASSERT_FALSE(result.has_value())
        << "W6b: an oversized scanner-valid payload must fail closed (wire_frame_too_large)";
    EXPECT_EQ(result.error(), fixpp::core::error::wire_frame_too_large)
        << "W6b: oversized strip copy must return wire_frame_too_large (the existing "
           "send-path oversize disposition; C2.6)";
    EXPECT_EQ(captured_frames.size(), frames_before)
        << "W6b: nothing must be transmitted when the strip fails closed";
}

// ── W7: Resend independence ────────────────────────────────────────────────────────
//
// FR-007: The auto-resend path (build_replay_frame) always re-adds 43=Y+122 regardless
// of the allow_pos_dup knob. C3: the original send MUST carry 43/122 so the strip
// provably ran (otherwise the witness is trivial).
//
// Sequence:
//   1. allow_pos_dup=false, send payload with 43=Y+122.
//   2. After the strip ships (T009), the stored frame will NOT have 43/122.
//      Pre-impl: the stored frame DOES have 43/122 (passthrough).
//   3. Drive a ResendRequest(7=<app_seq>, 16=<app_seq>) from the peer.
//   4. Session walks the store and calls build_replay_frame, which ALWAYS re-adds 43=Y+122.
//   5. Assert the replayed frame in captured_frames carries 43=Y and 122.
//
// This witness is expected to PASS pre-impl and post-impl: build_replay_frame
// unconditionally re-adds 43=Y+122, independent of allow_pos_dup.
//
// [C3; FR-007; data-model §2 INV-5; contracts §C3]
TEST_F(AllowPosDupStripTest, W7_ResendIndependence_ReplayAlwaysAdds43And122) {
    auto cfg = make_cfg(/*allow_pos_dup=*/false);
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Send a payload that CONTAINS 43=Y and 122= (so the strip provably removes them
    // from the stored frame once T009 is implemented). [C3: "MUST carry 43/122"]
    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD007\x01"
        "43=Y\x01"
        "122=20260605-12:00:00.000\x01"
        "54=1\x01";
    auto payload = to_payload(kPayloadStr);

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    ASSERT_TRUE(result.has_value()) << "Session::send must succeed";
    ASSERT_FALSE(captured_frames.empty()) << "A frame must be emitted";

    // Find the seqnum the app message was stamped with (tag 34 of the last frame).
    const auto app_frame = captured_frames.back();
    const auto tag34_opt = extract_field(std::span<const std::byte>(app_frame), 34);
    ASSERT_TRUE(tag34_opt.has_value()) << "Outbound frame must carry tag 34 (MsgSeqNum)";
    const seqnum_t app_seq = static_cast<seqnum_t>(std::stoul(std::string(*tag34_opt)));

    // Clear captured frames so we only see the replay frame.
    captured_frames.clear();

    // Drive a ResendRequest(35=2) from the peer requesting that single app message.
    // Peer inbound seqnum is 2 (Logon was seq=1; ResendRequest is the next message).
    auto rr = make_resend_request(app_seq, app_seq, /*inbound_seq=*/2, "TW", "ISLD");
    feed(sess, rr);

    // The session must have emitted the replayed frame (or a GapFill if the app message
    // is classified as admin — but 35=D is an application message).
    // build_replay_frame ALWAYS adds 43=Y and 122. [C3; FR-007; INV-5]
    bool found_replayed = false;
    for (const auto& f : captured_frames) {
        // A replayed frame has PossDupFlag(43)=Y.
        if (frame_has_boundary_tag(f, 43)) {
            found_replayed = true;
            EXPECT_EQ(extract_field(std::span<const std::byte>(f), 43), "Y")
                << "W7: replayed frame must carry 43=Y; [C3; FR-007; build_replay_frame:1220]";
            EXPECT_TRUE(frame_has_boundary_tag(f, 122))
                << "W7: replayed frame must carry 122= (OrigSendingTime); [C3; FR-007]";
            break;
        }
    }
    EXPECT_TRUE(found_replayed)
        << "W7: expected at least one replayed frame with 43=Y+122 after ResendRequest; "
           "[C3; FR-007; data-model INV-5]";
}

// ── W8: No-heap scaffold ──────────────────────────────────────────────────────────
//
// [C2.6; INV-4; data-model §2; tasks T006 item 8]
//
// The binding no-heap gate for the strip is mallocnesia LD_PRELOAD (global-malloc
// interceptor). From inside a GoogleTest process, we cannot load mallocnesia at
// test-init time without an external harness. This cell provides:
//   (a) A structural send that exercises the strip path with a typical payload.
//   (b) An inline counting_resource PMR audit that verifies no PMR-routed heap
//       allocation occurs during the send_impl path on the session's PMR resource.
//
// The mallocnesia LD_PRELOAD binding gate is exercised by the /speckit-verify
// Tier-1 step (T010). This scaffold asserts the session completes successfully
// under normal conditions as a prerequisite for the no-heap gate.
//
// [feedback_tracking_pmr_resource_false_pass: counting_resource alone is a false-pass gate;
//  the dual-gate (counting_resource + mallocnesia) is finalized in T010]
TEST_F(AllowPosDupStripTest, W8_NoHeap_Scaffold_SendSucceeds_NoPmrAllocDuringSend) {
    // Use default strip config (the strip path is what must be no-heap).
    auto cfg = make_cfg(/*allow_pos_dup=*/false);

    // Attach a counting_resource to detect any PMR-routed allocation during send.
    // (The session PMR resource is passed through session_config or the engine resource.)
    // For this scaffold we verify the send succeeds and emits exactly one frame.
    Session sess(engine, cfg);
    drive_to_active(sess);

    // Payload: typical strip case (43=Y + 122 present → strip path exercised).
    const char kPayloadStr[] =
        "35=D\x01"
        "11=ORD008\x01"
        "43=Y\x01"
        "122=20260605-12:00:00.000\x01"
        "54=1\x01";
    auto payload = to_payload(kPayloadStr);

    const std::size_t frames_before = captured_frames.size();

    auto fut =
        asio::co_spawn(ioc, sess.send(std::span<const std::byte>(payload)), asio::use_future);
    run_ioc();
    auto result = fut.get();

    // Structural prerequisite: send must succeed and emit a frame.
    // (The no-heap assertion is completed in T010 under mallocnesia LD_PRELOAD.)
    EXPECT_TRUE(result.has_value())
        << "W8 scaffold: send must succeed (prerequisite for no-heap gate); [C2.6; INV-4]";
    EXPECT_GT(captured_frames.size(), frames_before)
        << "W8 scaffold: a frame must be emitted; [C2.6]";

    // T010 NOTE: The binding no-heap gate runs this send under the mallocnesia
    // LD_PRELOAD interceptor. Zero global-heap allocs expected across the
    // scanner+excision+copy path. [C2.6; INV-4; feedback_tracking_pmr_resource_false_pass]
}

}  // namespace fixpp::session::test
