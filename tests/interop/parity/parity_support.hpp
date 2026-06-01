// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/interop/parity/parity_support.hpp — 016 US3 parity-witness support.
//
// Shared FIX-frame builders + an acceptor-session-to-Active harness for the
// reference-unit-test-parity GAP-closure witnesses (US3, FR-016/FR-017). These
// witnesses are STANDALONE — no counterparty, no live transport. They drive
// crafted admin frames through Session::on_inbound_frame() and observe fixpp's
// behavior via the sync transport_send_ capture + seqnum_mgr_test_access()
// (FIXPP_TEST_HOOKS), mirroring tests/session/test_inbound_sequence_reset.cpp +
// test_recovery_admin_span_gapfill.cpp (the proven recovery harness).
//
// [const §XV.9]: tests/-only.
#pragma once

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace fixpp::interop::parity {

using namespace std::chrono_literals;

inline std::string field(int tag, std::string_view val) {
    return std::to_string(tag) + "=" + std::string(val) + "\x01";
}

inline std::vector<std::byte> make_fix_frame(std::string_view begin_string,
                                             std::string_view msg_type, std::uint32_t seq,
                                             std::string_view sender, std::string_view target,
                                             std::string_view extra = {}) {
    std::string body;
    body += field(35, msg_type);
    body += field(34, std::to_string(seq));
    body += field(49, sender);
    body += field(52, "20240101-00:00:00.000");
    body += field(56, target);
    if (!extra.empty()) {
        body += std::string(extra);
    }

    std::string msg;
    msg += "8=" + std::string(begin_string) + "\x01";
    msg += "9=" + std::to_string(body.size()) + "\x01";
    msg += body;
    unsigned int cs = 0;
    for (unsigned char c : msg) {
        cs += c;
    }
    cs &= 0xFFU;
    char csbuf[5];
    std::snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    msg += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(msg.size());
    for (char c : msg) {
        frame.push_back(static_cast<std::byte>(c));
    }
    return frame;
}

inline std::vector<std::byte> make_logon(std::string_view bs, std::uint32_t seq, std::string_view s,
                                         std::string_view t, int hbt = 30) {
    std::string extra;
    extra += field(98, "0");
    extra += field(108, std::to_string(hbt));
    return make_fix_frame(bs, "A", seq, s, t, extra);
}

// ResendRequest(35=2) with BeginSeqNo(7)/EndSeqNo(16).
inline std::vector<std::byte> make_resend_request(std::string_view bs, std::uint32_t seq,
                                                  std::string_view s, std::string_view t,
                                                  std::uint32_t begin_seqno,
                                                  std::uint32_t end_seqno) {
    std::string extra;
    extra += field(7, std::to_string(begin_seqno));
    extra += field(16, std::to_string(end_seqno));
    return make_fix_frame(bs, "2", seq, s, t, extra);
}

// SequenceReset(35=4) with NewSeqNo(36); GapFillFlag(123)=Y only when gap_fill.
inline std::vector<std::byte> make_sequence_reset(std::string_view bs, std::uint32_t seq,
                                                  std::string_view s, std::string_view t,
                                                  std::uint32_t new_seqno, bool gap_fill) {
    std::string extra;
    if (gap_fill) {
        extra += field(123, "Y");
    }
    extra += field(36, std::to_string(new_seqno));
    return make_fix_frame(bs, "4", seq, s, t, extra);
}

inline bool frame_is_msg_type(std::span<const std::byte> frame, std::string_view type) {
    std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    return wire.find("35=" + std::string(type) + "\x01") != std::string::npos;
}

// Outbound capture with an optional fail-injection: once fail_writes is set, the
// next transport_send_ throws — the session's transmit_async() wraps the sync
// send in try/catch and maps the throw to a write failure (session.cpp ~1781).
struct OutboundCapture {
    std::vector<std::vector<std::byte>> frames;
    bool fail_writes = false;

    void operator()(std::span<const std::byte> data) {
        if (fail_writes) {
            throw std::runtime_error{"injected transport_send_ failure (parity witness)"};
        }
        frames.emplace_back(data.begin(), data.end());
    }
    [[nodiscard]] std::size_t count_msg_type(std::string_view type) const {
        std::size_t n = 0;
        for (const auto& f : frames) {
            if (frame_is_msg_type(f, type)) {
                ++n;
            }
        }
        return n;
    }
};

// Acceptor session driven to Active over the sync transport_send_ path.
class ParityAcceptorFixture : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};
    OutboundCapture capture;

    void SetUp() override {
        auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        auto stp = fixpp::core::steady_time_point{};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_acceptor_cfg() {
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
        return cfg;
    }

    fixpp::core::expected_t<void> run_open(fixpp::session::Session& s) {
        auto fut = asio::co_spawn(ioc, s.open(), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    fixpp::core::expected_t<void> feed(fixpp::session::Session& s,
                                       std::span<const std::byte> frame) {
        auto fut = asio::co_spawn(ioc, s.on_inbound_frame(frame), asio::use_future);
        ioc.run_for(100ms);
        ioc.restart();
        return fut.get();
    }

    // Drive to Active; after inbound Logon(seq=1) next-expected-inbound = 2.
    bool drive_to_active(fixpp::session::Session& s) {
        if (!run_open(s).has_value()) {
            return false;
        }
        (void)feed(s, make_logon("FIX.4.2", 1, "TW", "ISLD"));
        return s.state() == fixpp::session::fsm_state::Active;
    }

    std::uint32_t next_inbound(fixpp::session::Session& s) {
        return s.seqnum_mgr_test_access().next_inbound_unsafe();
    }
};

}  // namespace fixpp::interop::parity
