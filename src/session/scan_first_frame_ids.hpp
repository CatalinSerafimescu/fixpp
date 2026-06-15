// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/scan_first_frame_ids.hpp — internal session header.
//
// FirstFrameIds struct + scan_first_frame_ids inline free function, extracted
// from the engine.cpp anonymous namespace to enable direct unit testing (040
// US2 Phase 4 T011/T012).
//
// Previously defined in engine.cpp anonymous namespace; moved here as `inline`
// to allow the test translation unit to include and call the function without a
// separate compiled object. engine.cpp includes this header (replacing the
// anonymous-namespace definitions). The test includes it via the
// ${CMAKE_SOURCE_DIR}/src include path added to the test target.
//
// Internal header: NOT under include/fixpp/ (not part of the public API).
// Do NOT include from public headers or library consumers.
//
// Anchors: spec.md FR-007/FR-007a; research.md D-3; tasks.md T011/T012.
#pragma once

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/tag_scan.hpp>  // 040 US2: accumulate_tag_digit shared bounded-tag helper
#include <span>
#include <string_view>

namespace fixpp::session::detail {

// Minimal SOH-delimited scanner for CompID resolution (T012).
// Extracts begin_string (tag 8), sender_comp_id (tag 49), target_comp_id (tag 56)
// from a raw FIX frame bytes (views into the frame buffer — caller keeps it alive).
struct FirstFrameIds {
    std::string_view begin_string;
    std::string_view sender_comp_id;
    std::string_view target_comp_id;
};

[[nodiscard]] inline FirstFrameIds scan_first_frame_ids(std::span<const std::byte> frame) noexcept {
    FirstFrameIds ids;
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    std::size_t i = 0;
    const std::size_t n = frame.size();

    while (i < n) {
        std::uint32_t tag = 0;
        bool tag_ok = true;
        while (i < n && frame[i] != EQ && frame[i] != SOH) {
            auto c = static_cast<unsigned char>(frame[i]);
            // 040 US2 (FR-007a): digit-class check BEFORE the helper
            // (accumulate_tag_digit precondition: c must be '0'..'9'). Without
            // the bound, a forged token wraps uint32 and aliases a security tag
            // (49/56). The if/else-if shape keeps the existing tag_ok disposition
            // and guards against a future fold-into-helper that would silently
            // accept/dispatch non-numeric tokens.
            if (c < '0' || c > '9') {
                tag_ok = false;
            } else if (!fixpp::wire::accumulate_tag_digit(tag, c)) {
                // Accumulated value would exceed 0xFFFF (16-bit FIX tag bound).
                // Reject the field so it is never dispatched under the aliased tag.
                // Fixes the unguarded open-coded accumulate (research.md D-3).
                tag_ok = false;
            }
            ++i;
        }
        if (i >= n || frame[i] != EQ || !tag_ok) {
            while (i < n && frame[i] != SOH) ++i;
            if (i < n) ++i;
            continue;
        }
        ++i;  // skip '='
        std::size_t vstart = i;
        while (i < n && frame[i] != SOH) ++i;
        std::string_view val{reinterpret_cast<const char*>(frame.data() + vstart), i - vstart};
        if (i < n) ++i;  // skip SOH
        if (tag == 8) ids.begin_string = val;
        if (tag == 49) ids.sender_comp_id = val;
        if (tag == 56) ids.target_comp_id = val;
    }
    return ids;
}

}  // namespace fixpp::session::detail
