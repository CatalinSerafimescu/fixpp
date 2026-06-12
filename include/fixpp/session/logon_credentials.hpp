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
    constexpr std::string_view kMidTag   = "\x01" "554=";  // SOH + tag + '='
    constexpr std::string_view kStartTag = "554=";          // at frame position 0
    constexpr std::string_view kMask     = "***";

    std::string result;
    result.reserve(frame.size());

    std::size_t pos = 0;
    while (pos < frame.size()) {
        // Find the next 554 field boundary.
        std::size_t val_start = 0;    // position of the first byte of the value

        // Check mid-frame occurrence first (SOH-anchored).
        auto mid = frame.find(kMidTag, pos);

        // Check frame-start occurrence only when pos==0.
        bool has_start = (pos == 0) &&
                         (frame.size() >= kStartTag.size()) &&
                         (frame.compare(0, kStartTag.size(), kStartTag) == 0);

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

}  // namespace fixpp::session
