// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_537_second_too_high.cpp
//
// fixpp#537 (batch B28) — reproduction only, no fix.
//
// Claim under test: while a session awaits the replay its own ResendRequest
// asked for, a SECOND too-high frame that is not PossDup ends the session.
//
// Expected behaviour: FIX Session Layer (Nov 2020 errata), §4.10 "FIX session
// state matrix", precedence 12, "Awaiting/ Processing Response to
// ResendRequest(35=2)", Initiator Y, Acceptor Y: "Process requested
// MsgSeqNum(34) with PossDupFlag(43)=Y resent messages and/or SequenceReset(35=4)
// gap fill messages from counterparty. Queue incoming messages with MsgSeqNum(34)
// too high."
//
// Shape (the issue's first acceptance item), run on BOTH roles:
//   1. Session Active at NextNumIn = N.
//   2. Peer sends 34 = N+5. Assert a ResendRequest with BeginSeqNo(7) = N.
//   3. Before any replay, peer sends 34 = N+6, not PossDup.
//   4. Assert the session is still Active, sent no Logout, and recovers once the
//      peer's replay covers N..N+6.
//
// Cells per role:
//   OneTooHigh         control: step 3 omitted. Proves the too-high frame type
//                      reaches the seqnum guard (the ResendRequest is its
//                      witness) and that the recovery replay works.
//   SecondTooHigh      claim: step 3 with a TestRequest(35=1).
//   SecondTooHighHb    characterisation: step 3 with a Heartbeat(35=0), which
//                      takes a different arm after the seqnum check.
//
// The discriminating assertion is `state() == Active`. The suspected fatal
// path returns success and emits no Logout, so neither the return value nor a
// Logout count can tell the two outcomes apart.
//
// mock_clock: nothing here depends on elapsed time. HeartBtInt is long so the
// liveness loop stays parked for the whole cell.

#include <gtest/gtest.h>

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
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <future>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"
#include "support/session_test_access.hpp"

using namespace std::chrono_literals;

namespace fixpp::session::test {

namespace {

constexpr std::string_view kSt52 = "20240101-00:00:00.000";

std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                  std::string_view extra = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=TW\x01";
    body += "52=" + std::string(kSt52) + "\x01";
    body += "56=ISLD\x01";
    body += std::string(extra);
    std::string full =
        "8=FIX.4.2\x01"
        "9=" +
        std::to_string(body.size()) + "\x01" + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    char csbuf[8];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs & 0xFFU);
    full += "10=" + std::string(csbuf) + "\x01";
    std::vector<std::byte> out;
    out.reserve(full.size());
    for (char c : full) out.push_back(static_cast<std::byte>(c));
    return out;
}

std::vector<std::byte> test_request(std::uint32_t seq) {
    return make_frame("1", seq, "112=PEER-TR\x01");
}

// SequenceReset-GapFill from the peer's replay: 34 = begin, NewSeqNo(36) = end + 1.
std::vector<std::byte> gap_fill(std::uint32_t begin, std::uint32_t new_seq_no) {
    const std::string extra =
        "43=Y\x01"
        "122=" +
        std::string(kSt52) +
        "\x01"
        "123=Y\x01"
        "36=" +
        std::to_string(new_seq_no) + "\x01";
    return make_frame("4", begin, extra);
}

// Value of tag `tag` in a SOH-delimited frame, matched at a field boundary.
std::string field(std::string_view frame, std::uint32_t tag) {
    const std::string needle = "\x01" + std::to_string(tag) + "=";
    auto pos = frame.find(needle);
    if (pos == std::string_view::npos) return {};
    pos += needle.size();
    auto end = frame.find('\x01', pos);
    return std::string(frame.substr(pos, end == std::string_view::npos ? frame.npos : end - pos));
}

struct Capture {
    std::vector<std::string> frames;
    [[nodiscard]] std::vector<std::string> of_type(std::string_view msg_type) const {
        std::vector<std::string> out;
        for (const auto& f : frames) {
            if (field(f, 35) == msg_type) out.push_back(f);
        }
        return out;
    }
};

enum class Second { none, test_request, heartbeat };

class SecondTooHigh : public ::testing::TestWithParam<session_role> {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    SessionConfig make_cfg(Capture& cap) {
        SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = 30s;
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.role = GetParam();
        cfg.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
        cfg.transport_send = [&cap](std::span<const std::byte> f) {
            cap.frames.emplace_back(reinterpret_cast<const char*>(f.data()), f.size());
        };
        return cfg;
    }

    template <class Coro>
    fixpp::core::expected_t<void> run_sync(Coro coro, const char* site) {
        auto fut = asio::co_spawn(ioc, std::move(coro), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 200ms, site)) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock, site);
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << site;
            return std::unexpected(fixpp::test_support::kWindowMissSentinel);
        }
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(Session& s, const std::vector<std::byte>& f) {
        return run_sync(s.on_inbound_frame(std::span<const std::byte>{f}), "SecondTooHigh::feed");
    }

    // Both roles: open, then the peer's Logon(34=1). The initiator answers it as
    // the Logon ack; the acceptor answers with its own Logon.
    bool drive_to_active(Session& s) {
        if (!run_sync(s.open(), "SecondTooHigh::open")) return false;
        (void)feed(s, make_frame("A", 1,
                                 "98=0\x01"
                                 "108=30\x01"));
        return s.state() == fsm_state::Active;
    }

    void run_cell(Second second) {
        Capture cap;
        auto cfg = make_cfg(cap);
        Session s{engine, cfg};
        ASSERT_TRUE(drive_to_active(s)) << "setup: session did not reach Active";

        const seqnum_t n = session_test_access::seqnum_mgr(s).next_inbound_unsafe();
        cap.frames.clear();

        // Step 2: first too-high frame → ResendRequest from N.
        (void)feed(s, test_request(n + 5));
        const auto rr = cap.of_type("2");
        ASSERT_EQ(rr.size(), 1U) << "control precondition: the first too-high TestRequest must "
                                    "produce exactly one ResendRequest";
        ASSERT_EQ(field(rr.front(), 7), std::to_string(n))
            << "ResendRequest BeginSeqNo(7) must be NextNumIn";
        ASSERT_EQ(s.state(), fsm_state::Active);

        // Step 3: a second too-high frame, not PossDup, before any replay.
        std::uint32_t last = n + 5;
        if (second != Second::none) {
            last = n + 6;
            (void)feed(s, second == Second::heartbeat ? make_frame("0", last) : test_request(last));
            EXPECT_EQ(s.state(), fsm_state::Active)
                << "second non-PossDup too-high frame (34=" << last
                << ", 35=" << (second == Second::heartbeat ? "0" : "1")
                << ") while awaiting the replay "
                << "of 34=" << n << ".. ended the session: state=" << static_cast<int>(s.state())
                << ". SL2020 §4.10 precedence 12: \"Queue incoming messages with MsgSeqNum(34) "
                   "too high.\"";
            EXPECT_TRUE(cap.of_type("5").empty()) << "no Logout may be sent";
        }

        // Step 4: the peer's replay covers N..last; the session must be back in step.
        (void)feed(s, gap_fill(n, last + 1));
        EXPECT_EQ(s.state(), fsm_state::Active) << "after the replay through 34=" << last;
        EXPECT_EQ(session_test_access::seqnum_mgr(s).next_inbound_unsafe(), last + 1)
            << "NextNumIn after the replay through 34=" << last;
        (void)feed(s, make_frame("0", last + 1));
        EXPECT_EQ(s.state(), fsm_state::Active) << "in-sequence frame after recovery";
    }
};

}  // namespace

TEST_P(SecondTooHigh, OneTooHigh) { run_cell(Second::none); }
TEST_P(SecondTooHigh, SecondTooHigh) { run_cell(Second::test_request); }
TEST_P(SecondTooHigh, SecondTooHighHb) { run_cell(Second::heartbeat); }

INSTANTIATE_TEST_SUITE_P(BothRoles, SecondTooHigh,
                         ::testing::Values(session_role::initiator, session_role::acceptor),
                         [](const ::testing::TestParamInfo<session_role>& i) {
                             return i.param == session_role::initiator ? "Initiator" : "Acceptor";
                         });

}  // namespace fixpp::session::test
