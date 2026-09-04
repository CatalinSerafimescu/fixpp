// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/thorny/recovery/qfj-626-resend-recomputes-checksum_test.cpp
//   — 016 Gate-B/r1 [US2] thorny corpus C-101.
//
// Provenance: quickfix-j#626 (closed-with-fix). Upstream bug: QuickFIX-J
// replayed stored raw bytes verbatim, so a stored frame with a stale/wrong
// BodyLength(9) or CheckSum(10) would be replayed with the wrong values,
// violating [FIX-SL §4.3.5].
//
// fixpp disposition: PASS — differentiator.
// build_replay_frame() (session.cpp ~L1017) explicitly SKIPS tags 9 and 10
// from the stored bytes and calls wire::Writer::commit() which recomputes
// BodyLength(9) and CheckSum(10) from scratch. A stored frame with deliberately
// wrong 9= and 10= is therefore replayed with CORRECT 9= and 10=.
//
// This witness stores a single outbound application frame (35=D) whose
// BodyLength(9) and CheckSum(10) are deliberately wrong, feeds a ResendRequest,
// and asserts:
//   1. The replayed frame carries the CORRECT 9= (computed from actual body length).
//   2. The replayed frame carries the CORRECT 10= (computed checksum).
//   3. The replayed frame carries PossDupFlag(43)=Y.
//   4. The replayed frame carries OrigSendingTime(122)=.
//
// category: persistence/recovery. disposition: pass (differentiator).
// spec_ref [FIX-SL §4.3.5].

#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

#include "parity/parity_support.hpp"
#include "support/extract_tag.hpp"
#include "support/pump_until_ready.hpp"

// ── #289: bounded pumps ──────────────────────────────────────────────────────
//
// Where a site in this file is migrated it uses `run_window_then_ready` plus a
// miss-branch drain (tests/support/pump_until_ready.hpp). The window is PRESERVED:
// the hazard #289 names is the UNCONDITIONAL `get()`, not the fixed window.
//
// The site label passed to `run_window_then_ready` is the FORCING SEAM: exporting
// FIXPP_FORCE_WINDOW_MISS=<label> makes exactly that site take its miss branch, with
// no source edit and no rebuild. It is a WEAKER witness than textual mutation and
// does not replace it -- see the primitive.
//
// Rationale and the teardown-shape rule live at the primitive, not duplicated here
// (#324).

using namespace std::chrono_literals;
using fixpp::session::direction_t;
using fixpp::session::MessageStore;
using fixpp::session::MessageStoreFactory;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

namespace fixpp::interop::thorny {
namespace {

// Build a FIX frame with deliberately WRONG BodyLength(9) and CheckSum(10).
// The body itself is correct; only 9= and 10= are poisoned.
static std::vector<std::byte> make_frame_with_wrong_9_10(std::string_view begin_string,
                                                          std::string_view msg_type,
                                                          std::uint32_t seq,
                                                          std::string_view sender,
                                                          std::string_view target,
                                                          std::string_view extra = {}) {
    using namespace fixpp::interop::parity;
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    if (!extra.empty()) body += std::string(extra);

    // Correct frame first (to know the right body length).
    std::string correct_msg;
    correct_msg += "8=" + std::string(begin_string) + "\x01";
    correct_msg += "9=" + std::to_string(body.size()) + "\x01";
    correct_msg += body;
    unsigned int correct_cs = 0;
    for (unsigned char c : correct_msg) correct_cs += c;
    correct_cs &= 0xFFU;

    // Now build with WRONG 9= (body_len + 99) and WRONG 10= (checksum + 7).
    std::string poisoned_msg;
    poisoned_msg += "8=" + std::string(begin_string) + "\x01";
    poisoned_msg += "9=" + std::to_string(body.size() + 99U) + "\x01";  // wrong body length
    poisoned_msg += body;
    char wrong_cs_buf[5];
    std::snprintf(wrong_cs_buf, sizeof(wrong_cs_buf), "%03u",
                  static_cast<unsigned int>((correct_cs + 7U) & 0xFFU));  // wrong checksum
    poisoned_msg += "10=" + std::string(wrong_cs_buf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(poisoned_msg.size());
    for (char c : poisoned_msg) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Store double that returns a single application frame with wrong 9= and 10=.
class PoisonedChecksumStore final : public MessageStore {
public:
    explicit PoisonedChecksumStore(std::vector<std::byte> frame, seqnum_t seq)
        : MessageStore(flush_thunk_for<PoisonedChecksumStore>()),
          frame_(std::move(frame)),
          seq_(seq) {}

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
        const seqnum_t hi = (end == 0) ? seq_ : end;
        for (seqnum_t k = begin; k <= hi && k <= seq_; ++k) {
            if (k == seq_) {
                auto r = co_await visitor.on_frame(
                    k, std::span<const std::byte>{frame_.data(), frame_.size()});
                if (!r) co_return std::unexpected(r.error());
                if (*r != visit_result::cont) break;
            }
        }
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool /*increment*/) noexcept override {
        co_return fixpp::core::expected_t<seqnum_t>{dir == direction_t::outbound ? (seq_ + 1U)
                                                                                 : seqnum_t{1}};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

private:
    std::vector<std::byte> frame_;
    seqnum_t seq_;
};

class PoisonedChecksumStoreFactory final : public MessageStoreFactory {
public:
    explicit PoisonedChecksumStoreFactory(std::vector<std::byte> frame, seqnum_t seq)
        : frame_(std::move(frame)), seq_(seq) {}

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        return std::unique_ptr<MessageStore>(new PoisonedChecksumStore(frame_, seq_));
    }

private:
    std::vector<std::byte> frame_;
    seqnum_t seq_;
};

using fixpp::test_support::extract_tag;

// Compute the correct BodyLength for a FIX message body (fields after 9= up to
// and including the SOH before 10=). Per FIX spec: body = bytes from first
// field after BodyLength(9) to and including the delimiter before CheckSum(10).
static std::string compute_correct_body_length(std::span<const std::byte> frame) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    // Body starts after the second SOH (end of tag 9 field).
    auto pos9 = wire.find("9=");
    if (pos9 == std::string::npos) return {};
    auto soh_after_9 = wire.find('\x01', pos9);
    if (soh_after_9 == std::string::npos) return {};
    std::size_t body_start = soh_after_9 + 1;
    // Body ends at the start of "10=" field (exclusive of the '1').
    auto pos10 = wire.rfind("10=");
    if (pos10 == std::string::npos || pos10 <= body_start) return {};
    std::size_t body_len = pos10 - body_start;
    return std::to_string(body_len);
}

// Compute the correct CheckSum for a FIX message (sum of all bytes before 10=,
// modulo 256, formatted as 3 digits).
static std::string compute_correct_checksum(std::span<const std::byte> frame) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    auto pos10 = wire.rfind("10=");
    if (pos10 == std::string::npos) return {};
    unsigned int cs = 0;
    for (std::size_t i = 0; i < pos10; ++i) cs += static_cast<unsigned char>(wire[i]);
    cs &= 0xFFU;
    char buf[5];
    std::snprintf(buf, sizeof(buf), "%03u", cs);
    return buf;
}

// Fixture wrapping ParityAcceptorFixture to inject the poisoned-checksum store.
class Qfj626Fixture : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    fixpp::interop::parity::OutboundCapture capture;

    static constexpr seqnum_t kStoredSeq = 2U;  // one stored outbound at seq=2

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg() {
        // Build the stored frame: app message (35=D) with wrong 9= and 10=.
        auto poisoned = make_frame_with_wrong_9_10("FIX.4.2", "D", kStoredSeq, "ISLD", "TW",
                                                   fixpp::interop::parity::field(11, "ORD-C101"));

        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.transport_send = [this](std::span<const std::byte> d) { capture(d); };
        cfg.role = fixpp::session::session_role::acceptor;
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.store_factory =
            std::make_shared<PoisonedChecksumStoreFactory>(std::move(poisoned), kStoredSeq);
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 100ms,
                                                        "Qfj626Fixture::run_open")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "Qfj626Fixture::run_open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "Qfj626Fixture::run_open";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frm) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frm), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 100ms, "Qfj626Fixture::feed")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, "Qfj626Fixture::feed");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "Qfj626Fixture::feed";
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    bool drive_to_active(fixpp::session::Session& s) {
        if (!run_open(s).has_value()) return false;
        (void)feed(s, fixpp::interop::parity::make_logon("FIX.4.2", 1, "TW", "ISLD"));
        return s.state() == fixpp::session::fsm_state::Active;
    }
};

// C-101 witness: stored frame with wrong 9=/10= is replayed with correct 9=/10=.
//
// fixpp's build_replay_frame() skips tags 9 and 10 from the stored bytes and
// lets wire::Writer::commit() recompute them. This test proves that guarantee
// by injecting a stored frame whose 9= and 10= are deliberately poisoned and
// asserting the replayed output has the CORRECT values.
TEST_F(Qfj626Fixture, ResendReplay_RecomputesBodyLengthAndChecksum) {
    fixpp::session::Session s{engine, make_cfg()};
    ASSERT_TRUE(drive_to_active(s)) << "precondition: session must reach Active";

    // Feed a ResendRequest(35=2) asking for seq 2..2 from the peer.
    (void)feed(s, fixpp::interop::parity::make_resend_request("FIX.4.2", 2, "TW", "ISLD", 2, 2));

    // Find the replayed frame (35=D with 43=Y).
    bool found_replay = false;
    for (const auto& frame : capture.frames) {
        std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
        if (wire.find("35=D\x01") == std::string::npos) continue;
        if (wire.find("43=Y\x01") == std::string::npos) continue;
        found_replay = true;

        // Assert PossDupFlag(43)=Y present (precondition check).
        EXPECT_NE(wire.find("43=Y\x01"), std::string::npos) << "replay must carry PossDupFlag(43)=Y";
        // Assert OrigSendingTime(122) present.
        EXPECT_NE(wire.find("122="), std::string::npos) << "replay must carry OrigSendingTime(122)";

        // Compute expected correct BodyLength from the actual replayed frame bytes.
        std::string expected_bl =
            compute_correct_body_length(std::span<const std::byte>{frame.data(), frame.size()});
        ASSERT_FALSE(expected_bl.empty()) << "could not locate BodyLength boundary in replay";
        std::string actual_bl =
            extract_tag(std::span<const std::byte>{frame.data(), frame.size()}, 9);
        EXPECT_EQ(actual_bl, expected_bl)
            << "BodyLength(9) in replay must be recomputed correctly, not the stored wrong value";

        // Compute expected correct CheckSum from the actual replayed frame bytes.
        std::string expected_cs =
            compute_correct_checksum(std::span<const std::byte>{frame.data(), frame.size()});
        ASSERT_FALSE(expected_cs.empty()) << "could not locate CheckSum boundary in replay";
        std::string actual_cs =
            extract_tag(std::span<const std::byte>{frame.data(), frame.size()}, 10);
        EXPECT_EQ(actual_cs, expected_cs)
            << "CheckSum(10) in replay must be recomputed correctly, not the stored wrong value";

        break;
    }
    EXPECT_TRUE(found_replay)
        << "a ResendRequest must trigger a replayed frame (35=D with 43=Y) from the store";
}

}  // namespace
}  // namespace fixpp::interop::thorny
