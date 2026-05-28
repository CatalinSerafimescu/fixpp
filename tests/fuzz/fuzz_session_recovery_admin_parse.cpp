// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/fuzz/fuzz_session_recovery_admin_parse.cpp — T021 [US1] Phase 3
//
// libFuzzer harness for the session recovery + admin parse surface.
//
// Feeds random byte sequences into Session::on_inbound_frame() driving the
// session through the Framer + session FSM. Covers:
//   - Logon with ResetSeqNumFlag(141)=Y
//   - ResendRequest(2) inbound (triggers reply_to_inbound_resend_request)
//   - SequenceReset(4) with GapFillFlag (triggers process_inbound_sequence_reset)
//   - Heartbeat(0) with TestReqID(112) (triggers validate_inbound_heartbeat_testreqid)
//   - Logout(5) (triggers drive_logout path)
//
// All paths must: no crash / no UB / no memory corruption / no deadlock.
// The harness is NOT a behavioral correctness test; behavioral correctness is
// in T014–T019.
//
// Anchors: spec.md §US1 / FR-009..FR-016; [const §VII.7]; plan.md §T021;
//   [const §IX.4]; contracts/reconnect_fsm.hpp.
//
// Build: cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON
//   then: build/linux-clang-asan/bin/fuzz_session_recovery_admin_parse
//         -max_len=512 -runs=10000

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>

#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>

// These headers are relative because fuzz binaries have
//   target_include_directories(...PRIVATE "${CMAKE_SOURCE_DIR}/tests").
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

using namespace std::chrono_literals;

// ── Per-invocation harness state ──────────────────────────────────────────────
//
// We allocate a fresh Session per LLVMFuzzerTestOneInput call to avoid
// state leakage across invocations (session is not re-entrant and the FSM
// has per-session seqnum state).
//
// The io_context + mock_clock are also fresh per call; they are lightweight
// and the harness is expected to run O(10K) short paths.

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size < 4) return 0;  // Need at least a tag-value pair

    // The first byte encodes the initial "preamble style" (affect the
    // Logon/no-Logon branching so the fuzzer explores both the
    // NotConnected→Active path and the post-Active recovery paths).
    const std::uint8_t preamble = data[0];
    const std::uint8_t* payload = data + 1;
    const std::size_t   payload_len = size - 1;

    asio::io_context ioc;
    auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{};
    auto clk = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());

    fixpp::core::EngineConfig engine;
    engine.clock    = clk;
    engine.executor = ioc.get_executor();

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id    = "ISLD";
    cfg.target_comp_id    = "TW";
    cfg.begin_string      = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile  = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary        = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    // Swallow outbound frames (we don't care about the content in fuzz mode).
    cfg.transport_send = [](std::span<const std::byte>) {};
    cfg.role = fixpp::session::session_role::acceptor;

    fixpp::session::Session sess(engine, cfg);

    // Open the session.
    {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        try { (void)fut.get(); } catch (...) { return 0; }
    }

    // Optionally drive through a Logon first (preamble bit 0 set).
    if (preamble & 0x01) {
        // Minimal Logon frame (well-formed enough to pass framer).
        constexpr std::string_view logon_wire =
            "8=FIX.4.2\x01" "9=67\x01"
            "35=A\x01" "34=1\x01" "49=TW\x01" "52=20240101-00:00:00.000\x01"
            "56=ISLD\x01" "98=0\x01" "108=30\x01" "10=";
        // Compute a rough checksum (fuzzer doesn't need correctness here;
        // the framer will reject malformed frames gracefully).
        const char* logon_str = logon_wire.data();
        unsigned int cs = 0;
        for (const char* p = logon_str; *p; ++p) cs += static_cast<unsigned char>(*p);
        cs &= 0xFF;
        char full[256];
        snprintf(full, sizeof(full), "%s%03u\x01", logon_str, cs);
        auto buf = std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(full), strlen(full));
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(buf), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        try { (void)fut.get(); } catch (...) { return 0; }
    }

    // Feed the fuzzer payload. The Framer will reject garbage frames gracefully.
    // We feed up to 3 sub-spans split by the second byte (preamble>>1 encodes
    // the split point) to exercise multi-frame sequences.
    std::size_t split = (preamble >> 1) % (payload_len + 1);

    auto feed_span = [&](const std::uint8_t* p, std::size_t len) {
        if (len == 0) return;
        auto buf = std::span<const std::byte>(reinterpret_cast<const std::byte*>(p), len);
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(buf), asio::use_future);
        ioc.run_for(50ms);
        ioc.restart();
        try { (void)fut.get(); } catch (...) {}
    };

    feed_span(payload, split);
    if (split < payload_len) {
        feed_span(payload + split, payload_len - split);
    }

    return 0;
}
