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
[[nodiscard]] constexpr bool accumulate_tag_digit(std::uint32_t& tag, unsigned char c) noexcept {
    std::uint32_t const d = static_cast<std::uint32_t>(c - '0');
    if (tag > (0xFFFFU - d) / 10U) {  // pre-multiply bound; cannot itself overflow
        return false;
    }
    tag = (tag * 10U) + d;
    return true;
}

// Appends decimal digit `c` ('0'..'9') to a NON-TAG counted value `acc` (a
// Length+Data byte count or a repeating-group cardinality), SATURATING at the
// caller-supplied `cap` so no uint32 wrap can occur — the bound is checked
// BEFORE the multiply. Once `acc` would exceed `cap` it is pinned to `cap` and
// false is returned; further calls keep it at `cap` (idempotent saturate). This
// is the shared bound the wire-hostile-input review (W-P2-1c / W-P3-1) requires
// for the three previously-unbounded numeric scanners (offset_table dlen,
// parser.hpp parse_u32, validator declared_count). Saturation (not wrap) is the
// security property: a lying over-large length/count can never fold down to a
// small plausible value that would defeat a downstream bound/equality check.
//
// Precondition: `c` is an ASCII decimal digit ('0'..'9') AND `cap >= 9` (so
// `cap - d` cannot underflow). Both call domains satisfy cap>=9 (frame size or
// UINT32_MAX). Distinct from accumulate_tag_digit, which bounds to the fixed
// 16-bit tag space and signals REJECT (its callers dispose on false); here the
// contract is SATURATE (callers may ignore the bool and read the pinned `acc`).
[[nodiscard]] constexpr bool accumulate_bounded(std::uint32_t& acc, unsigned char c,
                                                std::uint32_t cap) noexcept {
    std::uint32_t const d = static_cast<std::uint32_t>(c - '0');
    if (acc > (cap - d) / 10U) {  // pre-multiply bound; cap>=9 => (cap-d) cannot underflow
        acc = cap;
        return false;
    }
    acc = (acc * 10U) + d;
    return true;
}

// ---------------------------------------------------------------------------
// Compile-time boundary correctness (normative — FR-001 / contracts/tag-scan-helper.md).
// These static_asserts pin the exact property the defective scan_frame_header:1493
// guard lacked, evaluated at compile time so a regressing edit is a build error.
// ---------------------------------------------------------------------------

// 65535 — maximum valid FIX tag. Must be accepted; end value must be 65535.
static_assert(
    [] {
        std::uint32_t t = 0;
        for (char c : "65535") {
            if (c == '\0') break;
            if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return false;
        }
        return t == 65535U;
    }(),
    "accumulate_tag_digit: 65535 must be accepted with value 65535");

// 65536 — one beyond the 16-bit bound. Must be rejected at the last digit.
static_assert(
    [] {
        std::uint32_t t = 0;
        for (char c : "65536") {
            if (c == '\0') break;
            if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return true;
        }
        return false;  // if never rejected, the check failed
    }(),
    "accumulate_tag_digit: 65536 must be rejected");

// 429496729649 — wrap-and-continue vector (aliases tag 49 without the bound check).
// Must be rejected well before any uint32 wrap can occur.
static_assert(
    [] {
        std::uint32_t t = 0;
        for (char c : "429496729649") {
            if (c == '\0') break;
            if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return true;
        }
        return false;  // if never rejected, the bound is wrong
    }(),
    "accumulate_tag_digit: wrap-and-continue 429496729649 must be rejected");

// 000000000034 — zero-padded tag. Must be accepted; end value must be 34.
static_assert(
    [] {
        std::uint32_t t = 0;
        for (char c : "000000000034") {
            if (c == '\0') break;
            if (!accumulate_tag_digit(t, static_cast<unsigned char>(c))) return false;
        }
        return t == 34U;
    }(),
    "accumulate_tag_digit: zero-padded 000000000034 must yield 34");

// ---------------------------------------------------------------------------
// accumulate_bounded boundary correctness (W-P2-1c / W-P3-1). Width-independent
// proof the saturating bound holds — the 32-bit heap-OOB escalation and the
// declared_count wrap both reduce to "a lying value can never wrap to a small
// plausible one." Evaluated at compile time on every lane.
// ---------------------------------------------------------------------------

// A value within cap accumulates exactly (cap generous).
static_assert(
    [] {
        std::uint32_t v = 0;
        for (char c : "12345") {
            if (c == '\0') break;
            if (!accumulate_bounded(v, static_cast<unsigned char>(c), 0xFFFFFFFFU)) return false;
        }
        return v == 12345U;
    }(),
    "accumulate_bounded: an in-cap value must accumulate exactly");

// THE wrap vector: 4294967298 = 2^32 + 2. An unbounded uint32 scan folds this to
// 2 (the W-P3-1 declared_count exploit). Saturating at UINT32_MAX must yield
// UINT32_MAX (≠ 2), never the wrapped small value.
static_assert(
    [] {
        std::uint32_t v = 0;
        for (char c : "4294967298") {
            if (c == '\0') break;
            (void)accumulate_bounded(v, static_cast<unsigned char>(c), 0xFFFFFFFFU);
        }
        return v == 0xFFFFFFFFU;  // saturated, NOT 2
    }(),
    "accumulate_bounded: 2^32+2 must saturate to UINT32_MAX, never wrap to 2");

// Saturation at a small cap: a declared length far larger than the frame must
// pin to cap (feeds the subtraction bound at the counted-Data site so no
// size_t address wrap is possible on any width).
static_assert(
    [] {
        std::uint32_t v = 0;
        bool tripped = false;
        for (char c : "999999") {
            if (c == '\0') break;
            if (!accumulate_bounded(v, static_cast<unsigned char>(c), 100U)) tripped = true;
        }
        return tripped && v == 100U;
    }(),
    "accumulate_bounded: over-cap value must saturate to cap and signal false");

}  // namespace fixpp::wire
