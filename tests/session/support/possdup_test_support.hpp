// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/support/possdup_test_support.hpp
//
// Shared fixture + frame builders for the 021-inbound-possdup-origsendingtime
// unit tests (test_inbound_poss_dup_tolerance.cpp / _validation.cpp). Factored
// out of the two test files (they were byte-identical) per the /simplify reuse
// pass. Field extraction reuses the existing support/frame_field_extract.hpp
// helper rather than re-rolling it.

#pragma once

#include <gtest/gtest.h>

#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "frame_field_extract.hpp"         // sibling (tests/session/support/)
#include "support/minimal_dictionary.hpp"  // tests/support/ via -I tests
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

namespace fixpp::session::test {

// ── Frame builders ──────────────────────────────────────────────────────────────

// Build a minimal FIX 4.2 frame. extra_fields is inserted after the standard
// header fields (35/34/49/52/56) and before the checksum; it must be a valid
// SOH-terminated field string. 52 is a fixed timestamp so PossDup tests can
// construct 122 values relative to it (greater/equal).
inline std::vector<std::byte> make_frame(std::string_view msg_type, std::uint32_t seq,
                                         std::string_view sender, std::string_view target,
                                         std::string extra_fields = {}) {
    std::string body;
    body += "35=" + std::string(msg_type) + "\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    if (!extra_fields.empty()) {
        body += extra_fields;
    }

    std::string hdr;
    hdr += "8=FIX.4.2\x01";
    hdr += "9=" + std::to_string(body.size()) + "\x01";

    std::string full = hdr + body;
    unsigned int cs = 0;
    for (unsigned char c : full) cs += c;
    cs &= 0xFFU;
    char csbuf[4];
    snprintf(csbuf, sizeof(csbuf), "%03u", cs);
    full += "10=" + std::string(csbuf) + "\x01";

    std::vector<std::byte> frame;
    frame.reserve(full.size());
    for (char c : full) frame.push_back(static_cast<std::byte>(c));
    return frame;
}

// Logon frame (35=A), used to bring a session to Active.
inline std::vector<std::byte> make_logon(std::uint32_t seq, std::string_view sender,
                                         std::string_view target) {
    return make_frame("A", seq, sender, target, "98=0\x01" "108=30\x01");
}

// Possible-duplicate frame: poss_dup injects "43=Y\x01122=...\x01" with 122==52
// (valid; INV-4: equality is NOT Arm D).
inline std::vector<std::byte> make_possdup_frame(std::string_view msg_type, std::uint32_t seq,
                                                 std::string_view sender, std::string_view target,
                                                 bool poss_dup, std::string extra = {}) {
    std::string ef;
    if (poss_dup) {
        ef += "43=Y\x01";
        ef += "122=20240101-00:00:00.000\x01";
    }
    if (!extra.empty()) ef += extra;
    return make_frame(msg_type, seq, sender, target, ef);
}

// ── counting_resource (PMR alloc audit; follows the memory-store witness pattern) ──

class counting_resource final : public std::pmr::memory_resource {
public:
    explicit counting_resource(
        std::pmr::memory_resource* upstream = std::pmr::new_delete_resource()) noexcept
        : upstream_(upstream) {}

    [[nodiscard]] long long allocate_count() const noexcept {
        return count_.load(std::memory_order_relaxed);
    }
    void reset_count() noexcept { count_.store(0, std::memory_order_relaxed); }

private:
    void* do_allocate(std::size_t bytes, std::size_t align) override {
        count_.fetch_add(1, std::memory_order_relaxed);
        return upstream_->allocate(bytes, align);
    }
    void do_deallocate(void* p, std::size_t bytes, std::size_t align) noexcept override {
        upstream_->deallocate(p, bytes, align);
    }
    [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_;
    mutable std::atomic<long long> count_{0};
};

// ── CountingApplication — counts fromApp deliveries ─────────────────────────────

class CountingApplication : public fixpp::session::Application {
public:
    int from_app_calls{0};

    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>&,
        const fixpp::session::SessionId&) override {
        ++from_app_calls;
        return {};
    }
};

// ── Base fixture — established acceptor session driven to Active ────────────────

class PossDupTestBase : public ::testing::Test {
protected:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine{};

    // Outbound frames captured via the transport_send callback.
    std::vector<std::vector<std::byte>> captured_frames;

    void SetUp() override {
        using namespace std::chrono;
        auto utc = system_clock::time_point{} + seconds{1704067200};  // 2024-01-01
        auto stp = fixpp::core::steady_time_point{} + seconds{0};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
    }

    fixpp::session::SessionConfig make_cfg(bool redeliver = false) {
        fixpp::session::SessionConfig cfg;
        cfg.sender_comp_id = "ISLD";
        cfg.target_comp_id = "TW";
        cfg.begin_string = "FIX.4.2";
        cfg.heartbeat_interval = std::chrono::seconds{0};
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.executor_override = ioc.get_executor();
        cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::bilateral_lenient;
        cfg.redeliver_poss_dup = redeliver;
        cfg.transport_send = [this](std::span<const std::byte> frame) {
            captured_frames.emplace_back(frame.begin(), frame.end());
        };
        return cfg;
    }

    // Open acceptor session + feed Logon(seq=1) → Active (next expected inbound = 2).
    void drive_to_active(Session& sess) {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "PossDupTestBase::drive_to_active/open")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "PossDupTestBase::drive_to_active/open");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "PossDupTestBase::drive_to_active/open";
            return;
        }
        ASSERT_TRUE(fut.get().has_value()) << "open() failed";

        auto logon = make_logon(1, "TW", "ISLD");
        auto fut2 = asio::co_spawn(ioc, sess.on_inbound_frame(logon), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut2, std::chrono::milliseconds{200},
                                                        "PossDupTestBase::drive_to_active/logon")) {
            fixpp::test_support::cancel_and_drain_or_report(
                ioc, *clock, "PossDupTestBase::drive_to_active/logon");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss
                          << "PossDupTestBase::drive_to_active/logon";
            return;
        }
        ASSERT_TRUE(fut2.get().has_value()) << "Logon feed failed";
        ASSERT_EQ(sess.state(), fixpp::session::fsm_state::Active);
    }

    // Feed a frame and block until complete.
    void feed(Session& sess, const std::vector<std::byte>& frame) {
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(frame), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, std::chrono::milliseconds{200},
                                                        "PossDupTestBase::feed/frame")) {
            fixpp::test_support::cancel_and_drain_or_report(ioc, *clock,
                                                            "PossDupTestBase::feed/frame");
            ADD_FAILURE() << fixpp::test_support::kWindowMiss << "PossDupTestBase::feed/frame";
            return;
        }
        (void)fut.get();
    }

    // Whether any captured frame has MsgType == mt (reuses shared extract_field).
    [[nodiscard]] bool any_msg_type(std::string_view mt) const {
        for (auto& f : captured_frames) {
            if (test_support::extract_field(f, 35) == mt) return true;
        }
        return false;
    }

    [[nodiscard]] bool any_logout() const { return any_msg_type("5"); }
};

}  // namespace fixpp::session::test
