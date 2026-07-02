// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/reify_test_frame.hpp
//
// 2b cutover (004 T059) shared test helper. Builds a minimal valid FIX 4.4
// NewOrderSingle frame so the reify suites can exercise the REAL owning
// deep-copy (from_view) + lazy view() rebuild on a frame-backed
// MessageView<Index>. Production frame_view producer is the Framer
// ([2b §4.2]); MessageView<Index>'s ctor then builds the OffsetTable
// dict-free. Each caller owns the arena lifetime it needs (e.g. the
// source-arena-reset survival tests destroy frame_mr deliberately).
#pragma once

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace fixpp::test_support {

// ClOrdID(11)="ORD1", MsgType(35)="D", v44. BodyLength + CheckSum computed.
[[nodiscard]] inline std::vector<std::byte> make_nos_frame() {
    std::string const body = std::string("35=D\x01") + "34=1\x01" + "49=S\x01" + "56=T\x01" +
                             "11=ORD1\x01" + "55=AAPL\x01";
    std::string pre =
        std::string("8=FIX.4.4\x01") + "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// 057: shared frame assembler — prepends BeginString, computes 9=BodyLength +
// 10=CheckSum over the body. `begin_string` is the "8=...\x01" prefix; `body`
// is the SOH-terminated field sequence starting at 35=. Public so negative-path
// tests can assemble ad-hoc frames (unknown/absent MsgType).
[[nodiscard]] inline std::vector<std::byte> assemble_frame(std::string const& begin_string,
                                                           std::string const& body) {
    std::string pre = begin_string + "9=" + std::to_string(body.size()) + "\x01" + body;
    unsigned sum = 0;
    for (unsigned char c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// 057 (E-6) application frame siblings. reify() resolves the application
// version from the PROFILE (default_appl) or an in-frame ApplVerID(1128), NOT
// from the BeginString — so these carry 35=D/11=ORD1 (discriminating field
// read) and the version axis is driven by the profile the test passes.

// v42 NewOrderSingle 35=D, 11=ORD1.
[[nodiscard]] inline std::vector<std::byte> make_nos_frame_v42() {
    return assemble_frame("8=FIX.4.2\x01", std::string("35=D\x01") + "34=1\x01" + "49=S\x01" +
                                               "56=T\x01" + "11=ORD1\x01");
}

// v50sp2 NewOrderSingle 35=D, 11=ORD1 (FIXT.1.1 transport).
[[nodiscard]] inline std::vector<std::byte> make_nos_frame_v50sp2() {
    return assemble_frame("8=FIXT.1.1\x01", std::string("35=D\x01") + "34=1\x01" + "49=S\x01" +
                                                "56=T\x01" + "11=ORD1\x01");
}

// v44 AllocationReport 35=AS (multi-char MsgType), 70=ALLOC1 (AllocID) — the
// discriminating body-field read for the two-char dispatch arm.
[[nodiscard]] inline std::vector<std::byte> make_allocation_report_frame() {
    return assemble_frame("8=FIX.4.4\x01", std::string("35=AS\x01") + "34=1\x01" + "49=S\x01" +
                                               "56=T\x01" + "70=ALLOC1\x01");
}

// FIXT-transport application frame carrying explicit ApplVerID(1128)="9"
// (FIX50SP2) + 11=ORD1. Drives version resolution from the in-frame 1128, NOT
// the profile default (US1 Acceptance Scenario 4 / T013).
[[nodiscard]] inline std::vector<std::byte> make_fixt_app_applverid_frame() {
    return assemble_frame("8=FIXT.1.1\x01", std::string("35=D\x01") + "34=1\x01" + "1128=9\x01" +
                                                "49=S\x01" + "56=T\x01" + "11=ORD1\x01");
}

// FIXT-admin frame (single-char admin MsgType 35=A Logon) with SenderCompID(49)
// + TargetCompID(56) for the discriminating header-field read (US2 / T016).
[[nodiscard]] inline std::vector<std::byte> make_fixt_admin_frame() {
    return assemble_frame("8=FIXT.1.1\x01", std::string("35=A\x01") + "34=1\x01" + "49=SENDER\x01" +
                                                "56=TARGET\x01" + "98=0\x01" + "108=30\x01");
}

}  // namespace fixpp::test_support
