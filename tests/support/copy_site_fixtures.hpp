// SPDX-License-Identifier: AGPL-3.0-or-later
// tests/support/copy_site_fixtures.hpp
//
// Shared frame builders and comparisons for the copy-site cap cells of fixpp#493
// (`.specify/495-493-486-dict-reify-copy.md` §10, T-2..T-6). One definition for
// the C clone cells (tests/capi/message_write_test.cpp) and the C++ reify cells
// (tests/dictionary/reify_dispatch_test.cpp), so both sides copy the same shape.
//
// The frames carry a computed CheckSum, so they pass the Framer the reify factory
// re-frames its copy with, as well as `fixpp::wire::test::make_frame_view`
// (frame_view_factory.hpp), which the C clone cells slice them with.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fixpp/wire/group_view.hpp>    // wire::group_context
#include <fixpp/wire/offset_table.hpp>  // wire::OffsetTable::Config
#include <string>
#include <vector>

namespace fixpp::test_support {

// FIX.4.4 frame around `body`: `8=FIX.4.4|9=<len>|<body>10=<sum>|`.
[[nodiscard]] inline std::vector<std::byte> make_raw_fix44_frame(std::string const& body) {
    std::string const pre = "8=FIX.4.4\x01" + ("9=" + std::to_string(body.size()) + "\x01") + body;
    unsigned sum = 0;
    for (unsigned char const c : pre) {
        sum += c;
    }
    std::array<char, 8> chk{};
    std::snprintf(chk.data(), chk.size(), "10=%03u\x01", sum % 256U);
    std::string const full = pre + chk.data();
    std::vector<std::byte> out(full.size());
    std::memcpy(out.data(), full.data(), full.size());
    return out;
}

// MsgType(35)=D, a marker field SenderCompID(49)=SENDERID, then `n_occurrences`
// repeats of a plain non-group tag (1=x). Once `n_occurrences + 2` exceeds
// offset_table.hpp's `default_max_offset_entries`, a DEFAULT-cap parse of this
// frame fails its OffsetTable build while a raised-cap parse admits it.
[[nodiscard]] inline std::vector<std::byte> make_oversized_frame_for_clone_test(
    int n_occurrences) {
    std::string body =
        "35=D\x01"
        "49=SENDERID\x01";
    for (int i = 0; i < n_occurrences; ++i) {
        body += "1=x\x01";
    }
    return make_raw_fix44_frame(body);
}

// MsgType(35)=D with SenderCompID(49)=SENDERID and one NoPartyIDs(453) instance:
// PartyID(448)=P, then `n_party_id_source` repeats of PartyIDSource(447)=D, then
// PartyRole(452)=1. FIX44 registers 447/452 as NoPartyIDs members, so under a
// dict-backed parse the whole run is ONE instance of `n_party_id_source + 2`
// entries — the lever for OffsetTable::Config::max_group_entries_per_instance,
// which is enforced lazily on the group read.
[[nodiscard]] inline std::vector<std::byte> make_long_party_instance_frame(
    int n_party_id_source) {
    std::string body =
        "35=D\x01"
        "49=SENDERID\x01"
        "453=1\x01"
        "448=P\x01";
    for (int i = 0; i < n_party_id_source; ++i) {
        body += "447=D\x01";
    }
    body += "452=1\x01";
    return make_raw_fix44_frame(body);
}

// Member-wise comparisons — the types carry no operator== and this change adds
// none (note §10 "Comparisons").
[[nodiscard]] inline bool same_config(fixpp::wire::OffsetTable::Config const& a,
                                      fixpp::wire::OffsetTable::Config const& b) noexcept {
    return a.max_offset_entries == b.max_offset_entries &&
           a.max_group_entries_per_instance == b.max_group_entries_per_instance;
}

// `msg_type` by CONTENT (each side views its own message buffer), then `depth` and
// the `parent_path` prefix `[0, depth)`.
[[nodiscard]] inline bool same_group_context(fixpp::wire::group_context const& a,
                                             fixpp::wire::group_context const& b) noexcept {
    return a.msg_type == b.msg_type && a.depth == b.depth &&
           std::equal(a.parent_path.begin(), a.parent_path.begin() + a.depth,
                      b.parent_path.begin());
}

}  // namespace fixpp::test_support
