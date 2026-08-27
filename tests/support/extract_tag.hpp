// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/extract_tag.hpp
//
// Read one FIX field's value out of a raw frame, for tests that assert on what a
// Session actually put on the wire.
//
// Hoisted (issue #320) from TWELVE independent file-local copies. That census is
// the reason this exists: the copies had DIVERGED, and not cosmetically —
// 11 of the 12 lacked field-boundary anchoring and 9 of the 12 returned the rest
// of the buffer for an unterminated value. PR #314 fixed both defects in exactly
// one copy and PR #319 added the mutation-tested corpus for it, so the only copy
// that was instrumented was the only one that was already correct.
//
// ── The semantics, and why THIS pair ────────────────────────────────────────
//
// 1. A hit is accepted only at a FIELD BOUNDARY — frame start, or immediately
//    after an SOH. `wire.find("112=")` alone also matches inside `"9112="`, which
//    launders a different tag's value into the caller's corpus as if it were the
//    one asked for. On a rejected hit the search RESUMES past it rather than
//    giving up, so a decoy before the real field does not hide it.
//
// 2. An UNTERMINATED value (no SOH after it) yields `{}`, not the rest of the
//    buffer. Returning the tail means a truncated frame silently produces a
//    plausible-looking value built from whatever bytes followed.
//
// Both arms are mutation-tested. See
// `CrossSessionTestReqIDParser.RejectsNonCanonicalAndOverflowCorpora` in
// tests/session/test_test_request_id_cross_session_race.cpp, which pins the
// boundary rejection, the resume-past-a-decoy, the accept-at-frame-start and the
// unterminated arms, each with the inverted control that the OTHER arms keep
// passing. That test is this header's acceptance suite; do not change the
// behaviour here without re-reddening it.
//
// ⚠️ This is a TEST-SIDE reader for assertions, not a parser. It has no
// dictionary, does not honour data-length pairs (e.g. RawDataLength/RawData,
// where the value may legitimately contain an SOH byte), and will stop at the
// first SOH inside such a value. Every current caller reads short scalar
// administrative fields, where that cannot arise. Stated as the CONDITION rather
// than as a count of today's callers: a caller that needs a length-delimited
// field needs a real parser, not this.
//
// Not to be confused with tests/session/support/frame_field_extract.hpp, which is
// a different function (`extract_field` -> std::optional<std::string_view>, and
// requires a TARGET_OBJECTS link).

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace fixpp::test_support {

// Returns the value of `tag`, or "" if the frame has no boundary-anchored,
// SOH-terminated occurrence of it. `frame` need not be NUL-terminated.
//
// `std::vector<std::byte>` converts implicitly, so callers holding a captured
// frame can pass it directly.
[[nodiscard]] inline std::string extract_tag(std::span<const std::byte> frame, std::uint32_t tag) {
    if (frame.empty()) {
        return {};
    }
    const std::string wire(reinterpret_cast<const char*>(frame.data()), frame.size());
    const std::string needle = std::to_string(tag) + "=";
    for (auto pos = wire.find(needle); pos != std::string::npos;
         pos = wire.find(needle, pos + 1)) {
        if (pos != 0 && wire[pos - 1] != '\x01') {
            continue;  // e.g. "9112=" is not tag 112
        }
        const auto vstart = pos + needle.size();
        const auto end = wire.find('\x01', vstart);
        if (end == std::string::npos) {
            return {};
        }
        return wire.substr(vstart, end - vstart);
    }
    return {};
}

}  // namespace fixpp::test_support
