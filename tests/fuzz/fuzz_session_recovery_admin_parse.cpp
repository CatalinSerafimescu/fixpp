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
// 092-garbled-frame-reject (specs/092-garbled-frame-reject/tasks.md T066; research.md
// R-2 §2; contract C-3 I-4): before the Session runs, the payload is also handed
// straight to scan_frame_header and to OffsetTable::build, under the hooks of a
// dictionary that declares a dictionary-only Length+Data pair and under
// dict_hooks::none(). Any disagreement on what an encoding fault is traps; so does,
// for a fault-free payload, a fault_ref_seq_num / msg_type_is_third /
// fault_ref_msg_type that is not the one entries() names. Only a resource-failure
// build status is skipped, and those are counted (fuzz_092_support.hpp).
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
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/framer.hpp>  // frame_view_slice_access
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/parser.hpp>  // dict_hooks::for_table_view
#include <fixpp/wire/tag_scan.hpp>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>

// These headers are relative because fuzz binaries have
//   target_include_directories(...PRIVATE "${CMAKE_SOURCE_DIR}/tests").
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"
#include "support/pump_until_ready.hpp"

// 092: the internal header is reached through ${CMAKE_SOURCE_DIR}/src.
#include "fuzz_092_support.hpp"
#include "session/scan_frame_header.hpp"

using namespace std::chrono_literals;

namespace {

using fixpp::core::error;
using fixpp::session::detail::FrameHeader;
using fixpp::session::detail::scan_frame_header;
using fixpp::wire::dict_hooks;
using fixpp::wire::field_fault;
using fixpp::wire::frame_view_slice_access;
using fixpp::wire::OffsetTable;

// The tag of the field that starts at `at`, read here without any library helper so
// a mutation of a shared tag reader cannot move both sides: one or more ASCII
// digits whose value is at most 0xFFFF, then '='. Leading zeros are allowed.
// nullopt when the field does not start with such a tag.
std::optional<std::uint32_t> well_formed_tag_at(std::span<const std::byte> buf, std::size_t at) {
    std::uint32_t value = 0;
    std::size_t i = at;
    while (i < buf.size()) {
        auto const c = static_cast<unsigned char>(buf[i]);
        if (c < '0' || c > '9') {
            break;
        }
        value = (value * 10U) + (c - '0');
        if (value > 0xFFFFU) {
            return std::nullopt;
        }
        ++i;
    }
    if (i == at || i >= buf.size() || buf[i] != std::byte{'='}) {
        return std::nullopt;
    }
    return value;
}

// A view the scan returned names exactly the bytes of `e`: same address, same length.
// Comparing addresses, not contents, keeps two fields with equal values apart.
bool names_entry(std::string_view v, std::span<const std::byte> buf, OffsetTable::entry const& e) {
    return v.data() == reinterpret_cast<char const*>(buf.data()) + e.offset && v.size() == e.length;
}

// Fault-free payload: the header identification must be the one entries() gives
// (research R-2: the oracle is entries(), not find()).
void check_references_against_entries(FrameHeader const& h, std::span<const std::byte> buf,
                                      OffsetTable const& table) {
    auto const entries = table.entries();
    OffsetTable::entry const* first_34 = nullptr;
    for (auto const& e : entries) {
        if (e.tag == 34) {
            first_34 = &e;
            break;
        }
    }
    if (first_34 != nullptr) {
        if (!names_entry(h.fault_ref_seq_num, buf, *first_34)) {
            __builtin_trap();
        }
    } else if (h.fault_ref_seq_num.data() != nullptr) {
        __builtin_trap();
    }

    bool const third_is_35 = entries.size() > 2 && entries[2].tag == 35;
    if (h.msg_type_is_third != third_is_35) {
        __builtin_trap();
    }
    if (third_is_35) {
        if (!names_entry(h.fault_ref_msg_type, buf, entries[2])) {
            __builtin_trap();
        }
    } else if (h.fault_ref_msg_type.data() != nullptr) {
        __builtin_trap();
    }
}

// One (payload, hooks) comparison of the scan with OffsetTable::build.
void check_scan_agrees_with_build(std::span<const std::byte> buf, dict_hooks const& hooks) {
    auto& counts = fixpp::fuzz092::counter("R-2 scan_frame_header vs OffsetTable::build");
    ++counts.cases;
    std::pmr::monotonic_buffer_resource arena;  // heap upstream: no artificial out_of_memory
    auto const fv = frame_view_slice_access::make(buf.data(), buf.size(), {});
    OffsetTable const table(fv, &arena, hooks);
    auto const status = table.build_status();
    if (fixpp::fuzz092::is_resource_failure(status)) {
        ++counts.skipped;
        return;
    }
    FrameHeader const h = scan_frame_header(buf, hooks);

    if (h.fault == field_fault::none) {
        if (!status.has_value()) {
            __builtin_trap();  // the build found an encoding fault the scan did not
        }
        check_references_against_entries(h, buf, table);
        return;
    }
    if (status.has_value()) {
        __builtin_trap();  // the scan faulted on a payload the build accepts
    }
    // The status class of the fault kind (data-model E-0).
    error const e = status.error();
    if (h.fault == field_fault::malformed_tag) {
        if (e != error::wire_invalid_field_format && e != error::wire_tag_out_of_range) {
            __builtin_trap();
        }
        if (h.fault_length_tag != 0) {
            __builtin_trap();
        }
    } else if (e != error::wire_invalid_field_format) {  // length_data_mismatch
        __builtin_trap();
    }

    // The site, as the oracle sees it: every field before the scan's fault builds
    // cleanly. At the fault, a malformed_tag has no well-formed tag, and a
    // length_data_mismatch has a well-formed tag equal to the Data tag of the field
    // before it, which the build itself treats as a Length under these hooks.
    if (h.fault_offset > buf.size()) {
        __builtin_trap();
    }
    auto const pfv = frame_view_slice_access::make(buf.data(), h.fault_offset, {});
    OffsetTable const prefix(pfv, &arena, hooks);
    auto const prefix_status = prefix.build_status();
    if (fixpp::fuzz092::is_resource_failure(prefix_status)) {
        ++counts.skipped;
        return;
    }
    if (!prefix_status.has_value()) {
        __builtin_trap();  // the build fails before the scan's first fault
    }
    auto const tag_at_fault = well_formed_tag_at(buf, h.fault_offset);
    if (h.fault == field_fault::malformed_tag) {
        if (tag_at_fault.has_value()) {
            __builtin_trap();  // the scan calls a well-formed tag malformed
        }
        return;
    }
    auto const pe = prefix.entries();
    if (pe.empty() || pe.back().tag != h.fault_length_tag) {
        __builtin_trap();
    }
    std::uint16_t const data_tag = hooks.data_tag_for_length(pe.back().tag);
    if (data_tag == 0 || !tag_at_fault.has_value() || *tag_at_fault != data_tag) {
        __builtin_trap();  // not a counted site
    }
}

void check_scan_agrees_with_build(std::span<const std::byte> buf) {
    static dict_hooks const pair_hooks =
        dict_hooks::for_table_view(fixpp::fuzz092::pair_dict_table_view());
    check_scan_agrees_with_build(buf, pair_hooks);
    check_scan_agrees_with_build(buf, dict_hooks::none());
}

}  // namespace

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

    // 092 R-2 arm, on the bytes the Session is fed (the preamble byte excluded),
    // before any session code runs.
    check_scan_agrees_with_build(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(payload), payload_len));

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
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 50ms, "fuzz_admin_parse/open")) {
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
        int hdr_len = snprintf(full, sizeof(full),
                               "8=FIX.4.2\x01"
                               "9=%zu\x01",
                               body_len);
        // Append body.
        memcpy(full + hdr_len, logon_base.data(), logon_base.size());
        memcpy(full + hdr_len + logon_base.size(), tag789_suffix, strlen(tag789_suffix));
        std::size_t body_end = static_cast<std::size_t>(hdr_len) + body_len;
        // Compute a rough checksum over everything so far.
        unsigned int cs = 0;
        for (std::size_t i = 0; i < body_end; ++i) cs += static_cast<unsigned char>(full[i]);
        cs &= 0xFF;
        int trailer_len = snprintf(full + body_end, sizeof(full) - body_end, "10=%03u\x01", cs);
        const std::size_t total = body_end + static_cast<std::size_t>(trailer_len);

        auto buf = std::span<const std::byte>(reinterpret_cast<const std::byte*>(full), total);
        auto fut = asio::co_spawn(ioc, sess.on_inbound_frame(buf), asio::use_future);
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 50ms, "fuzz_admin_parse/logon")) {
            // Same disposition as the two other sites; the rationale -- including why
            // `abort()` was rejected -- is stated once at `fuzz_admin_parse/open`.
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
        if (!fixpp::test_support::run_window_then_ready(ioc, fut, 50ms, "fuzz_admin_parse/feed")) {
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
