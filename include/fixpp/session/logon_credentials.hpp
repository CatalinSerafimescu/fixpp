// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/logon_credentials.hpp
//
// 033-fixt-fix50sp2-session — T010.
//
// logon_credentials: surfaced value of parsed Username(553)/Password(554)
// fields from an inbound FIXT Logon (data-model.md E5 / research R6 / C8).
//
// The password is ALWAYS redacted in any operator<< / debug form (FR-011 /
// INV-FIXT-4). Both fields are owning strings so the value outlives the
// inbound wire frame.
//
// redact_tag554: shared tag-554 field redactor (C8). Takes a FIX frame as a
// string (SOH-delimited), replaces the *value* of any 554=<value> occurrence
// with "***", and returns the result. Applied at every persistence site (session
// logger/tap, transport transcript, golden writer) — not ad-hoc per site.

#pragma once

#include <cstddef>
#include <optional>
#include <ostream>
#include <span>
#include <string>

namespace fixpp::session {

// ── logon_credentials ────────────────────────────────────────────────────────
//
// Value type carrying the parsed 553/554 fields. Both are optional:
//  - populated when the counterparty included the field in its Logon;
//  - absent when the field was not present.
//
// operator<< redacts the password — the clear value never appears in any
// stream, log, or debug representation (FR-011 / INV-FIXT-4).
struct logon_credentials {
    std::optional<std::string> username;
    std::optional<std::string> password;

    // Streaming: prints username if present; password is replaced by "***"
    // (never the clear value). Conforms to FR-011 / INV-FIXT-4.
    friend std::ostream& operator<<(std::ostream& os, logon_credentials const& c) {
        os << "logon_credentials{username=";
        if (c.username.has_value()) {
            os << *c.username;
        } else {
            os << "(absent)";
        }
        os << ", password=";
        if (c.password.has_value()) {
            os << "***";  // redacted — never print the clear value (FR-011)
        } else {
            os << "(absent)";
        }
        os << "}";
        return os;
    }
};

// ── redact_tag554 ─────────────────────────────────────────────────────────────
//
// Shared tag-554 field redactor (C8 / FR-011). Replaces the value of every
// "554=<value>" occurrence in a SOH-delimited FIX frame string with "***".
// Detection is anchored on field boundaries (SOH '\x01' precedes every tag
// except the first) so a decoy containing "554=" inside a free-text value
// (e.g. inside tag 58= text) is NOT redacted — only a true 554 tag is matched.
//
// Algorithm: scan for '\x01' + "554=" (mid-frame) or "554=" at position 0
// (frame start). Replace the value bytes (up to the next SOH or end-of-string)
// with "***". Returns a new std::string.
//
// This function is the single canonical redaction site; T024 (US2) wires it
// into logger/transcript sites and T026 (US3) wires it into the golden writer.
[[nodiscard]] inline std::string redact_tag554(std::string const& frame) {
    constexpr std::string_view kMidTag =
        "\x01"
        "554=";                                     // SOH + tag + '='
    constexpr std::string_view kStartTag = "554=";  // at frame position 0
    constexpr std::string_view kMask = "***";

    std::string result;
    result.reserve(frame.size());

    std::size_t pos = 0;
    while (pos < frame.size()) {
        // Find the next 554 field boundary.
        std::size_t val_start = 0;  // position of the first byte of the value

        // Check mid-frame occurrence first (SOH-anchored).
        auto mid = frame.find(kMidTag, pos);

        // Check frame-start occurrence only when pos==0.
        bool has_start = (pos == 0) && (frame.size() >= kStartTag.size()) &&
                         (frame.starts_with(kStartTag));

        if (!has_start && mid == std::string::npos) {
            // No more 554 fields — copy the rest and stop.
            result.append(frame, pos, std::string::npos);
            break;
        }

        if (has_start) {
            // Frame-start match (only possible when pos==0, so always first).
            val_start = kStartTag.size();  // past "554="
        } else {
            // Mid-frame match.
            val_start = mid + kMidTag.size();  // past "\x01554="
        }

        // Append frame bytes up to and including "554=" (but not the value).
        result.append(frame, pos, val_start - pos);

        // Replace the value with the mask.
        result.append(kMask);

        // Advance past the original value (up to next SOH or EOS).
        std::size_t val_end = frame.find('\x01', val_start);
        if (val_end == std::string::npos) {
            pos = frame.size();
        } else {
            pos = val_end;  // the terminating SOH is not the value; keep it
        }
    }

    return result;
}

// ── frame_has_genuine_tag554 ─────────────────────────────────────────────────
//
// Detection-only sibling of mask_tag554_same_length_inplace (034 / C2 / R4).
// Returns true iff `frame` carries at least one genuine SOH-delimited 554 field
// (SAME anchoring rule as the masker: `\x01554=` mid-frame, or `554=` at offset
// 0; a `554=` substring inside another field's value is NOT a match). Const,
// zero-alloc, noexcept. Used by the persist-path maskability gate
// (Session::store_then_emit) so the overwhelming majority of outbound frames —
// which carry no 554 — are detected and stored as-is with no copy.
[[nodiscard]] inline bool frame_has_genuine_tag554(std::span<const std::byte> frame) noexcept {
    constexpr std::byte kSoh = std::byte{0x01};
    constexpr std::byte k5 = std::byte{'5'};
    constexpr std::byte k4 = std::byte{'4'};
    constexpr std::byte kEq = std::byte{'='};
    const std::size_t n = frame.size();

    // Frame-start occurrence: "554=" at offset 0.
    if (n >= 4 && frame[0] == k5 && frame[1] == k5 && frame[2] == k4 && frame[3] == kEq) {
        return true;
    }
    // Mid-frame occurrence: SOH + "554=".
    for (std::size_t i = 0; i + 5 <= n; ++i) {
        if (frame[i] == kSoh && frame[i + 1] == k5 && frame[i + 2] == k5 && frame[i + 3] == k4 &&
            frame[i + 4] == kEq) {
            return true;
        }
    }
    return false;
}

// ── mask_tag554_same_length_inplace ──────────────────────────────────────────
//
// Same-length, zero-allocation in-place masker for the FIX Password(554) field
// (034-credential-store-redaction / E1 / FR-003 / FR-008).
//
// Scans `frame` for every genuine SOH-delimited `554` field (same anchoring rule
// as redact_tag554: `\x01554=` mid-frame, or `554=` at offset 0). For each
// match, overwrites the value bytes (from just past `554=` up to the next `\x01`
// or end-of-frame) with `'*'` (0x2A) in place, preserving the byte count exactly.
//
// Returns `true` iff at least one genuine 554 field was found (even if the value
// extent was empty — zero bytes to overwrite still counts as a match). Returns
// `false` when no genuine 554 field exists, leaving the buffer unmodified.
//
// Invariants (E1):
//   I-E1-1 (length): frame.size() unchanged; no byte outside a 554 value modified.
//   I-E1-2 (idempotent): masking an already-masked frame is a no-op ('*' stays '*').
//   I-E1-3 (zero-alloc/noexcept): no heap allocation; no exceptions.
//   I-E1-4 (delimiter-safe): 554 value bytes cannot contain SOH or '=' (033 FQ-3
//     injection floor), so the value extent is unambiguous.
[[nodiscard]] inline bool mask_tag554_same_length_inplace(std::span<std::byte> frame) noexcept {
    // Field-boundary needles (byte literals — no implicit char→byte narrowing).
    // Mid-frame: SOH + '5' + '5' + '4' + '='  (5 bytes)
    // Frame-start: '5' + '5' + '4' + '='       (4 bytes)
    static constexpr std::byte kMid[5] = {std::byte{0x01}, std::byte{'5'}, std::byte{'5'},
                                          std::byte{'4'}, std::byte{'='}};
    static constexpr std::byte kStart[4] = {std::byte{'5'}, std::byte{'5'}, std::byte{'4'},
                                            std::byte{'='}};
    static constexpr std::size_t kMidLen = 5;
    static constexpr std::size_t kStartLen = 4;
    static constexpr std::byte kSoh = std::byte{0x01};
    static constexpr std::byte kStar = std::byte{0x2A};  // '*'

    const std::size_t n = frame.size();
    bool masked = false;
    std::size_t pos = 0;

    while (pos < n) {
        std::size_t val_start = 0;  // index of first value byte (just past '=')

        // Check frame-start occurrence (only valid when pos == 0).
        bool has_start_match = (pos == 0) && (n >= kStartLen) && (frame[0] == kStart[0]) &&
                               (frame[1] == kStart[1]) && (frame[2] == kStart[2]) &&
                               (frame[3] == kStart[3]);

        // Search for mid-frame occurrence '\x01554='.
        std::size_t mid_pos = std::string_view::npos;
        if (!has_start_match) {
            for (std::size_t i = pos; i + kMidLen <= n; ++i) {
                if (frame[i] == kMid[0] && frame[i + 1] == kMid[1] && frame[i + 2] == kMid[2] &&
                    frame[i + 3] == kMid[3] && frame[i + 4] == kMid[4]) {
                    mid_pos = i;
                    break;
                }
            }
        }

        if (!has_start_match && mid_pos == std::string_view::npos) {
            // No more genuine 554 fields — done.
            break;
        }

        // A genuine field was detected (even if empty value).
        masked = true;

        if (has_start_match) {
            val_start = kStartLen;  // past "554="
        } else {
            val_start = mid_pos + kMidLen;  // past "\x01554="
        }

        // Overwrite value bytes up to the next SOH or end-of-frame with '*'.
        std::size_t val_end = val_start;
        while (val_end < n && frame[val_end] != kSoh) {
            frame[val_end] = kStar;
            ++val_end;
        }

        // Resume from val_end (the terminating SOH, if present, is kept as the
        // anchor for the next mid-frame needle search — mirrors redact_tag554).
        pos = val_end;
    }

    return masked;
}

}  // namespace fixpp::session
