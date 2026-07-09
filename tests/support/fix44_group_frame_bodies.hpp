// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/fix44_group_frame_bodies.hpp
//
// 066-dict-backed-inbound-parse T001 — shared group-bearing FIX44
// ExecutionReport(35=8) body builders, reused by BOTH the real-Session-
// dispatch harness (tests/session/support/) and the C-ABI engine-loopback
// harness (tests/capi/).
//
// Group: NoLegs(555) — dict-registered for ExecutionReport(35=8) via the
// `InstrmtLegExecGrp` component (dictionaries/FIX44.xml:251, expanded at
// dictionaries/FIX44.xml:2830-2845); members used here are LegSymbol(600),
// LegSide(624), LegQty(687) from the `InstrumentLeg` component
// (dictionaries/FIX44.xml:2436+). See tests/support/fix44_dictionary.hpp for
// the full citation.
//
// Two body shapes (spec.md US1 Independent Test; contracts/inbound-parse.md
// C1/C3; tasks.md T001/T004):
//   (a) execution_report_two_legs_trailing_suffix() — NoLegs x2, followed by
//       a TRAILING outer field (TransactTime, tag 60) AFTER the group. The
//       trailing-absorption witness fixture (T004a).
//   (b) execution_report_interior_undeclared_tag_suffix() — the SAME shape,
//       but with an undeclared tag (9999 — absent from FIX44.xml entirely,
//       confirmed via grep) placed INTERIOR to NoLegs entry #1, between two
//       of its declared members (LegSymbol/600 and LegSide/624). The
//       FR-008/C3 interior-truncation witness fixture (T004b).
//
// Each "suffix" is the ExecutionReport-specific body content ONLY: it leads
// with NEITHER "35=" NOR any session header/trailer tag (8/9/34/49/52/56/10)
// — callers prepend "35=8\x01" (+ session header fields, for a full wire
// frame) or use the suffix directly as a C-ABI application payload (which
// must lead with "35=" and carry no session tags — see
// tests/capi/capi_loopback_support.hpp's make_app_payload contract note).
//
// RED-FIRST PRESERVATION (research.md Decision 6 / [const Art VII §3]):
// this header contains NO assertions — it is pure byte-builder scaffolding.
// The membership/extent/trailing-absence assertions belong to T004/T005/T010
// and MUST be written and observed RED later, against the unchanged
// dict-free parse.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "support/app_message_read_scaffold.hpp"  // fixpp_test_support::make_frame (checksum/BodyLength wrap)

namespace fixpp_test_support {

// ── (a) NoLegs x2, trailing TransactTime(60) AFTER the group ────────────────
inline std::string execution_report_two_legs_trailing_suffix() {
    std::string b;
    b += "37=ORDID-1\x01";  // OrderID (required)
    b += "17=EXEC-1\x01";   // ExecID (required)
    b += "150=0\x01";       // ExecType (required)
    b += "39=0\x01";        // OrdStatus (required)
    b += "55=AAPL\x01";     // Symbol (Instrument component, required)
    b += "54=1\x01";        // Side (required)
    b += "151=0\x01";       // LeavesQty (required)
    b += "14=0\x01";        // CumQty (required)
    b += "6=0\x01";         // AvgPx (required)
    b += "555=2\x01";       // NoLegs = 2
    b += "600=LEGA\x01";    // leg #1: LegSymbol
    b += "624=1\x01";       // leg #1: LegSide
    b += "687=100\x01";     // leg #1: LegQty
    b += "600=LEGB\x01";    // leg #2: LegSymbol
    b += "624=2\x01";       // leg #2: LegSide
    b += "687=200\x01";     // leg #2: LegQty
    b += "60=20240101-00:00:00.000\x01";  // TRAILING outer field, AFTER the group
    return b;
}

// ── (b) Same shape, with an undeclared tag (9999) INTERIOR to leg entry #1 ──
// (between the declared LegSymbol(600) and LegSide(624) members).
inline std::string execution_report_interior_undeclared_tag_suffix() {
    std::string b;
    b += "37=ORDID-1\x01";
    b += "17=EXEC-1\x01";
    b += "150=0\x01";
    b += "39=0\x01";
    b += "55=AAPL\x01";
    b += "54=1\x01";
    b += "151=0\x01";
    b += "14=0\x01";
    b += "6=0\x01";
    b += "555=2\x01";
    b += "600=LEGA\x01";        // leg #1: LegSymbol (declared)
    b += "9999=UNDECLARED\x01";  // undeclared tag, INTERIOR to leg #1
    b += "624=1\x01";           // leg #1: LegSide (declared, AFTER the undeclared tag)
    b += "687=100\x01";         // leg #1: LegQty (declared)
    b += "600=LEGB\x01";        // leg #2: LegSymbol
    b += "624=2\x01";           // leg #2: LegSide
    b += "687=200\x01";         // leg #2: LegQty
    b += "60=20240101-00:00:00.000\x01";
    return b;
}

// ── Full inbound wire frame (session header + suffix), for the real-Session
// dispatch harness. `sender`/`target` follow the SenderCompID(49)/
// TargetCompID(56) convention of the existing session test fixtures
// (test_application_outbound.cpp's make_raw_frame).
inline std::vector<std::byte> make_execution_report_frame(std::string_view suffix,
                                                           std::uint32_t seq,
                                                           std::string_view sender,
                                                           std::string_view target,
                                                           std::string_view begin_string = "FIX.4.4") {
    std::string body = "35=8\x01";
    body += "34=" + std::to_string(seq) + "\x01";
    body += "49=" + std::string(sender) + "\x01";
    body += "52=20240101-00:00:00.000\x01";
    body += "56=" + std::string(target) + "\x01";
    body += std::string(suffix);
    return fixpp_test_support::make_frame(begin_string, body);
}

// ── C-ABI application payload (leads with "35=", NO session header tags) ────
// for fixpp_session_send — see capi_loopback_support.hpp's make_app_payload
// contract note (session stamps 8/9/34/49/52/56/10 itself).
inline std::vector<std::uint8_t> make_execution_report_app_payload(std::string_view suffix) {
    std::string p = "35=8\x01" + std::string(suffix);
    return std::vector<std::uint8_t>(p.begin(), p.end());
}

}  // namespace fixpp_test_support
