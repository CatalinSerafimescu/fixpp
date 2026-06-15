// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/tag_scan.hpp — shared bounded-tag accumulate helper.
// Single source of the 16-bit FIX tag bound (SC-004): all five live-inbound
// hand-rolled tag scanners call this helper instead of open-coding the bound.
// Reference shapes: src/wire/framer.cpp:120 (BodyLength pre-multiply bound)
//                   src/session/session.cpp:1588 (seqnum pre-multiply bound)
// Dependency-free leaf: <cstdint> only (wire layer, no session/core includes).
// Contract: contracts/tag-scan-helper.md; research: specs/040-*/research.md D-1.
#pragma once
#include <cstdint>

namespace fixpp::wire {

// Appends decimal digit `c` ('0'..'9') to `tag`, bounding to the 16-bit FIX
// tag space (0xFFFF). Returns false (without advancing the accumulator past the
// in-progress value) if appending `c` would make `tag` exceed 0xFFFF — detected
// BEFORE the multiply so no uint32 wrap can occur. The caller MUST dispose on
// false per its own control flow and MUST NOT use `tag` as a valid tag.
//
// Precondition: `c` is an ASCII decimal digit ('0'..'9'). Callers validate
// digit-class BEFORE calling (FR-007a — do NOT fold the digit check into this
// helper; sites that call on non-digits breach the precondition and may produce
// a NEW aliasing bug).
[[nodiscard]] constexpr bool accumulate_tag_digit(std::uint32_t& tag,
                                                  unsigned char c) noexcept {
    std::uint32_t const d = static_cast<std::uint32_t>(c - '0');
    if (tag > (0xFFFFU - d) / 10U) {  // pre-multiply bound; cannot itself overflow
        return false;
    }
    tag = (tag * 10U) + d;
    return true;
}

// ---------------------------------------------------------------------------
// Compile-time boundary correctness (normative — FR-001 / contracts/tag-scan-helper.md).
// These static_asserts pin the exact property the defective scan_frame_header:1493
// guard lacked, evaluated at compile time so a regressing edit is a build error.
// ---------------------------------------------------------------------------

// 65535 — maximum valid FIX tag. Must be accepted; end value must be 65535.
static_assert([] {
    std::uint32_t t = 0;
    for (char c : "65535") {
        if (c == '\0') break;
        if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return false;
    }
    return t == 65535U;
}(), "accumulate_tag_digit: 65535 must be accepted with value 65535");

// 65536 — one beyond the 16-bit bound. Must be rejected at the last digit.
static_assert([] {
    std::uint32_t t = 0;
    for (char c : "65536") {
        if (c == '\0') break;
        if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return true;
    }
    return false;  // if never rejected, the check failed
}(), "accumulate_tag_digit: 65536 must be rejected");

// 429496729649 — wrap-and-continue vector (aliases tag 49 without the bound check).
// Must be rejected well before any uint32 wrap can occur.
static_assert([] {
    std::uint32_t t = 0;
    for (char c : "429496729649") {
        if (c == '\0') break;
        if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return true;
    }
    return false;  // if never rejected, the bound is wrong
}(), "accumulate_tag_digit: wrap-and-continue 429496729649 must be rejected");

// 000000000034 — zero-padded tag. Must be accepted; end value must be 34.
static_assert([] {
    std::uint32_t t = 0;
    for (char c : "000000000034") {
        if (c == '\0') break;
        if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return false;
    }
    return t == 34U;
}(), "accumulate_tag_digit: zero-padded 000000000034 must yield 34");

}  // namespace fixpp::wire
