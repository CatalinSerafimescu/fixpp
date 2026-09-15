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
#include <cstdint>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/length_data_carry.hpp>  // fixpp#426: counted Data values
#include <fixpp/wire/tag_scan.hpp>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>

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

namespace detail {

// Calls `on_value(vstart, vend)` with the value extent of every genuine
// Password(554) field in `frame`: a field whose tag bytes are exactly `554`
// (at offset 0 or right after a SOH).
//
// fixpp#426: fields are walked from the start, so a correctly counted Data value
// is stepped over as one unit and a `<SOH>554=` inside it is not a field — the
// rule used before, matching every SOH-anchored `554=`, masked such a value and
// corrupted the stored bytes a resend replays. If a count is malformed the walk
// cannot know where that value ends, so from that field on it falls back to the
// old rule: a real Password is over-masked rather than missed (design §4).
template <class OnValue>
void for_each_tag554_value(std::span<const std::byte> frame, fixpp::wire::dict_hooks const& hooks,
                           OnValue&& on_value) {
    constexpr std::byte kSoh{0x01};
    constexpr std::byte kEq{'='};
    std::size_t const n = frame.size();
    auto const value_end = [&](std::size_t from) {
        while (from < n && frame[from] != kSoh) {
            ++from;
        }
        return from;
    };
    auto const is_554_eq_at = [&](std::size_t p) {
        return p + 4 <= n && frame[p] == std::byte{'5'} && frame[p + 1] == std::byte{'5'} &&
               frame[p + 2] == std::byte{'4'} && frame[p + 3] == kEq;
    };

    // Every field starts at offset 0 or right after a SOH, so a frame with no such
    // `554=` holds no Password field and needs no walk (most outbound frames).
    bool maybe_554 = false;
    for (std::size_t p = 0; p + 4 <= n && !maybe_554; ++p) {
        maybe_554 = (p == 0 || frame[p - 1] == kSoh) && is_554_eq_at(p);
    }
    if (!maybe_554) {
        return;
    }

    fixpp::wire::length_data_carry carry;
    std::size_t i = 0;
    while (i < n) {
        std::size_t const field_start = i;
        std::uint32_t tag = 0;
        bool tag_ok = true;
        while (i < n && frame[i] != kEq && frame[i] != kSoh) {
            auto const c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9' || !fixpp::wire::accumulate_tag_digit(tag, c)) {
                tag_ok = false;
            }
            ++i;
        }
        if (i >= n || frame[i] != kEq || !tag_ok) {
            carry.reset();
            i = value_end(i);
            if (i < n) {
                ++i;
            }
            continue;
        }
        std::size_t const vstart = i + 1;
        auto const value = carry.read_value(frame, vstart, static_cast<std::uint16_t>(tag), hooks);
        if (!value) {
            for (std::size_t p = field_start; p < n; ++p) {
                if ((p == 0 || frame[p - 1] == kSoh) && is_554_eq_at(p)) {
                    on_value(p + 4, value_end(p + 4));
                }
            }
            return;
        }
        std::size_t const vend = value->end;
        if (is_554_eq_at(field_start) && i == field_start + 3) {
            on_value(vstart, vend);
        }
        i = vend < n ? vend + 1 : n;
    }
}

}  // namespace detail

// ── redact_tag554 ─────────────────────────────────────────────────────────────
//
// Shared tag-554 field redactor (C8 / FR-011). Returns a copy of `frame` with the
// value of every genuine Password(554) field replaced by "***". A genuine field is
// one detail::for_each_tag554_value reports: a `554=` inside another field's
// value (tag 58 free text, or a counted Data value — fixpp#426) is not redacted.
//
// This is the single canonical redaction site; T024 (US2) wires it into
// logger/transcript sites and T026 (US3) wires it into the golden writer. Those
// sites have no dictionary, so the pairs are the standard table alone.
[[nodiscard]] inline std::string redact_tag554(std::string const& frame) {
    constexpr std::string_view kMask = "***";

    std::string result;
    result.reserve(frame.size());
    std::size_t pos = 0;
    detail::for_each_tag554_value(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(frame.data()), frame.size()},
        fixpp::wire::dict_hooks::none(), [&](std::size_t vstart, std::size_t vend) {
            result.append(frame, pos, vstart - pos);
            result.append(kMask);
            pos = vend;  // the terminating SOH is not the value; keep it
        });
    result.append(frame, pos, std::string::npos);
    return result;
}

// ── frame_has_genuine_tag554 ─────────────────────────────────────────────────
//
// Detection-only sibling of mask_tag554_same_length_inplace (034 / C2 / R4).
// Returns true iff `frame` carries at least one genuine Password(554) field, by
// the same rule as the masker. Const, zero-alloc, noexcept. Used by the
// persist-path maskability gate (Session::store_then_emit) so the overwhelming
// majority of outbound frames — which carry no 554 — are stored as-is with no
// copy. `hooks` supplies the Length+Data pairs (fixpp#426).
[[nodiscard]] inline bool frame_has_genuine_tag554(
    std::span<const std::byte> frame,
    fixpp::wire::dict_hooks const& hooks = fixpp::wire::dict_hooks::none()) noexcept {
    bool found = false;
    detail::for_each_tag554_value(frame, hooks, [&](std::size_t, std::size_t) { found = true; });
    return found;
}

// ── mask_tag554_same_length_inplace ──────────────────────────────────────────
//
// Same-length, zero-allocation in-place masker for the FIX Password(554) field
// (034-credential-store-redaction / E1 / FR-003 / FR-008).
//
// Overwrites the value bytes of every genuine Password(554) field (see
// detail::for_each_tag554_value) with `'*'` (0x2A) in place, preserving the byte
// count exactly. `hooks` supplies the Length+Data pairs (fixpp#426).
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
[[nodiscard]] inline bool mask_tag554_same_length_inplace(
    std::span<std::byte> frame,
    fixpp::wire::dict_hooks const& hooks = fixpp::wire::dict_hooks::none()) noexcept {
    bool masked = false;
    detail::for_each_tag554_value(frame, hooks, [&](std::size_t vstart, std::size_t vend) {
        masked = true;
        for (std::size_t k = vstart; k < vend; ++k) {
            frame[k] = std::byte{0x2A};  // '*'
        }
    });
    return masked;
}

}  // namespace fixpp::session
