// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/send_header_tags.hpp — internal session header.
//
// The session's fixed send-side header set and its predicate, moved out of
// session.cpp unchanged in content (098-dialect-overlay, research R-7) so that the
// standalone census `dictionary_098_header_census_test` (W-7) reads the production
// set by including this header, without linking fixpp_session, as
// scan_frame_header.hpp was moved out for its tests. Self-contained: standard
// headers only.
//
// Internal header: NOT under include/fixpp/ (not part of the public API).
// Do NOT include from public headers or library consumers.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

namespace fixpp::session::detail {

// fixpp#422: StandardHeader fields. build_send_frame_ moves such a field ahead of the
// payload's body fields; a strict peer (QuickFIX-J UseDataDictionary=Y) rejects a
// header field that follows a body field (373=14). The set is the <header> of
// dictionaries/FIXT11.xml (a superset of FIX44.xml's) plus OnBehalfOfSendingTime(370),
// which QuickFIX-J's Message.isHeaderField counts. Unlike build_replay_frame's
// kReplayHeaderTags, which only needs to be a SUBSET of the real header, the send
// classification must be a SUPERSET of it: a header tag it misses stays behind the
// body. Since 098 (FR-022) that classification is this set unioned with the tags the
// session's dictionary declares in its header (`table_view::is_header_tag`), so the
// superset requirement falls on the union, and this set alone covers the vendored
// dictionaries' headers (W-7's session half re-derives that).
inline constexpr std::array<std::uint32_t, 34> kSendHeaderTags = {
    8,   9,   34,  35,  43,  49,  50,  52,  56,  57,  90,  91,  97,  115, 116,  122,  128,
    129, 142, 143, 144, 145, 212, 213, 347, 369, 370, 627, 628, 629, 630, 1128, 1129, 1156};

// True when `tag` is in the fixed set above.
inline bool is_send_header_tag(std::uint32_t tag) noexcept {
    return std::ranges::find(kSendHeaderTags, tag) != kSendHeaderTags.end();
}

}  // namespace fixpp::session::detail
