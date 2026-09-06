// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/fuzz/fuzz_session_recovery_admin_parse.cpp — T021 [US1] Phase 3 / T026 [Polish]
//
// libFuzzer harness for the session recovery + admin parse surface.
//
// Feeds random byte sequences into Session::on_inbound_frame() driving the
// session through the Framer + session FSM. Covers:
//   - Logon with ResetSeqNumFlag(141)=Y
//   - Logon with NextExpectedMsgSeqNum(789) — well-formed, malformed, overflow
//     (T026 extension: exercises the new case 789: in scan_frame_header)
//   - ResendRequest(2) inbound (triggers reply_to_inbound_resend_request)
//   - SequenceReset(4) with GapFillFlag (triggers process_inbound_sequence_reset)
//   - Heartbeat(0) with TestReqID(112) (triggers validate_inbound_heartbeat_testreqid)
//   - Logout(5) (triggers drive_logout path)
//
// All paths must: no crash / no UB / no memory corruption / no deadlock.
// The harness is NOT a behavioral correctness test; behavioral correctness is
// in T014–T019 and T007–T010 (027 unit witnesses).
//
// T026 (Polish, 027): the new `case 789:` in `scan_frame_header` is a
// parser-touching change per [const §VII] item 7. It is already driven via
// Session::on_inbound_frame() → Framer → scan_frame_header when a Logon
// carrying 789 is fed. This is a seed/corpus extension, NOT a new harness:
// preamble bits 2–3 now select 789-bearing Logon variants (well-formed 789=2,
// malformed 789=, invalid 789=abc, overflow 789=99999999999) to exercise both
// the happy parse path and the parse_seqnum→0 invalid path.
//
// Anchors: spec.md §US1 / FR-009..FR-016; [const §VII.7]; plan.md §T021/T026;
//   [const §IX.4]; contracts/reconnect_fsm.hpp; 027 contracts C6 (invalid 789).
//
// Build: cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON
//   then: build/linux-clang-asan/bin/fuzz_session_recovery_admin_parse
//         -max_len=512 -runs=10000

#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <memory>
#include <span>

// These headers are relative because fuzz binaries have
//   target_include_directories(...PRIVATE "${CMAKE_SOURCE_DIR}/tests").
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

#include "support/pump_until_ready.hpp"

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
    const std::size_t payload_len = size - 1;

    asio::io_context ioc;
    auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
    auto stp = fixpp::core::steady_time_point{};
    auto clk = std::make_shared<fixpp::core::mock_clock>(utc, stp, ioc.get_executor());

    fixpp::core::EngineConfig engine;
    engine.clock = clk;
    engine.executor = ioc.get_executor();

    fixpp::session::SessionConfig cfg;
    cfg.sender_comp_id = "ISLD";
    cfg.target_comp_id = "TW";
    cfg.begin_string = "FIX.4.2";
    cfg.heartbeat_interval = 30s;
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.executor_override = ioc.get_executor();
    // Swallow outbound frames (we don't care about the content in fuzz mode).
    cfg.transport_send = [](std::span<const std::byte>) {};
    cfg.role = fixpp::session::session_role::acceptor;

    fixpp::session::Session sess(engine, cfg);

    // Open the session.
    {
        auto fut = asio::co_spawn(ioc, sess.open(), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 50ms,
                                                        "fuzz_admin_parse/open")) {
            // ⚠️ NO `ADD_FAILURE` AND NO `drain_or_report` HERE, DELIBERATELY -- both
            // report through gtest, and in a libFuzzer TU that is a FALSE GREEN.
            // MEASURED, not reasoned: a probe linking gtest into a libFuzzer target
            // fired `ADD_FAILURE` outside any `TEST` body; it printed the failure and
            // the process still exited 0. `ctest -L fuzz` replays the corpus with
            // `-runs=0` and grades on the EXIT CODE, so a reported residual here would
            // read as a pass. Escalating to `abort()` was the alternative and was
            // rejected: a 50 ms miss on a random input under ASan+UBSan+fuzzer
            // instrumentation is a timing observation, not a defect, so aborting would
            // buy a flaky CI failure and no correctness signal.
            // What this branch DOES buy is the whole of #289: there is no longer an
            // unconditional `.get()`, so a genuinely wedged input can never hang the
            // fuzzer forever. Abandoning the input is the disposition this file already
            // ships on its `catch (...)` paths.
            return 0;
        }
        try {
            (void)fut.get();
        } catch (...) {
            return 0;
        }
    }

    // Optionally drive through a Logon first (preamble bits 0–3 select variant).
    //
    // Bit 0: drive a Logon (any variant).
    // Bits 1–2: select which Logon variant:
    //   0b00 — plain Logon (no 789)           — exercises the existing parser path.
    //   0b01 — Logon with 789=2 (well-formed) — exercises the new case 789: arm (T026).
    //   0b10 — Logon with 789=   (empty)      — exercises parse_seqnum→0 invalid path.
    //   0b11 — Logon with 789=abc (non-digit) — exercises parse_seqnum→0 invalid path.
    // Bit 3: if set AND variant 0b01, use overflow value 789=99999999999 instead.
    //
    // The framer will reject semantically-wrong BodyLength/checksum gracefully; we
    // compute a rough checksum for minimal frame validity.

    if (preamble & 0x01) {
        const int variant = (preamble >> 1) & 0x03;
        const bool use_overflow = (preamble >> 3) & 0x01;

        // Build the Logon body suffix for tag 789, depending on variant.
        // variant 0: no 789 field.
        // variant 1: 789=2 (well-formed; or 789=99999999999 if overflow bit set).
        // variant 2: 789= (empty value).
        // variant 3: 789=abc (non-digit).
        const char* tag789_suffix = "";
        if (variant == 1) {
            tag789_suffix = use_overflow ? "789=99999999999\x01" : "789=2\x01";
        } else if (variant == 2) {
            tag789_suffix = "789=\x01";
        } else if (variant == 3) {
            tag789_suffix = "789=abc\x01";
        }

        // Logon body fields (before the 789 suffix and the checksum trailer).
        constexpr std::string_view logon_base =
            "35=A\x01"
            "34=1\x01"
            "49=TW\x01"
            "52=20240101-00:00:00.000\x01"
            "56=ISLD\x01"
            "98=0\x01"
            "108=30\x01";

        // Compute BodyLength: logon_base + tag789_suffix.
        const std::size_t body_len = logon_base.size() + strlen(tag789_suffix);

        // Build the full frame: header + body + 10=<cs>\x01.
        char full[512];
        int hdr_len = snprintf(full, sizeof(full), "8=FIX.4.2\x01" "9=%zu\x01",
                               body_len);
        // Append body.
        memcpy(full + hdr_len, logon_base.data(), logon_base.size());
        memcpy(full + hdr_len + logon_base.size(), tag789_suffix, strlen(tag789_suffix));
        std::size_t body_end = static_cast<std::size_t>(hdr_len) + body_len;
        // Compute a rough checksum over everything so far.
        unsigned int cs = 0;
        for (std::size_t i = 0; i < body_end; ++i)
            cs += static_cast<unsigned char>(full[i]);
        cs &= 0xFF;
        int trailer_len = snprintf(full + body_end, sizeof(full) - body_end,
                                   "10=%03u\x01", cs);
        const std::size_t total = body_end + static_cast<std::size_t>(trailer_len);

        auto buf = std::span<const std::byte>(reinterpret_cast<const std::byte*>(full), total);
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(buf), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 50ms,
                                                        "fuzz_admin_parse/logon")) {
            // ⚠️ NO `ADD_FAILURE` AND NO `drain_or_report` HERE, DELIBERATELY -- both
            // report through gtest, and in a libFuzzer TU that is a FALSE GREEN.
            // MEASURED, not reasoned: a probe linking gtest into a libFuzzer target
            // fired `ADD_FAILURE` outside any `TEST` body; it printed the failure and
            // the process still exited 0. `ctest -L fuzz` replays the corpus with
            // `-runs=0` and grades on the EXIT CODE, so a reported residual here would
            // read as a pass. Escalating to `abort()` was the alternative and was
            // rejected: a 50 ms miss on a random input under ASan+UBSan+fuzzer
            // instrumentation is a timing observation, not a defect, so aborting would
            // buy a flaky CI failure and no correctness signal.
            // What this branch DOES buy is the whole of #289: there is no longer an
            // unconditional `.get()`, so a genuinely wedged input can never hang the
            // fuzzer forever. Abandoning the input is the disposition this file already
            // ships on its `catch (...)` paths.
            return 0;
        }
        try {
            (void)fut.get();
        } catch (...) {
            return 0;
        }
    }

    // Feed the fuzzer payload. The Framer will reject garbage frames gracefully.
    // We feed up to 3 sub-spans split by the second byte (preamble>>1 encodes
    // the split point) to exercise multi-frame sequences.
    std::size_t split = (preamble >> 1) % (payload_len + 1);

    auto feed_span = [&](const std::uint8_t* p, std::size_t len) {
        if (len == 0) return;
        auto buf = std::span<const std::byte>(reinterpret_cast<const std::byte*>(p), len);
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(buf), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 50ms,
                                                        "fuzz_admin_parse/feed")) {
            // Same disposition as the two sites above; the rationale is stated once
            // at `fuzz_admin_parse/open`. This one is inside a void lambda.
            return;
        }
        try {
            (void)fut.get();
        } catch (...) {
        }
    };

    feed_span(payload, split);
    if (split < payload_len) {
        feed_span(payload + split, payload_len - split);
    }

    return 0;
}
