// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/peek_fixt_route.hpp — internal session header.
//
// 099-fixt-appl-version-validation (data-model E-7's "The peek's oracle", research R-5, FR-012):
// what a FIXT frame's header says before anything parses it, which is what picks the table that
// will. `scan_frame_header` cannot do this: its hooks come from that table, so what it splits
// depends on the answer.
//
// One rule on every shape. peek_fixt_route's walk uses S's hooks through
// `length_data_carry::read_value`, the primitive `scan_frame_header` uses, and reads a tag exactly
// as that scan does, so a frame this cannot read is one the scan, on S's route, faults or garbles
// on in the same place. It stops at the first tag that is neither a header tag of S nor a standard
// header tag.
//
// MsgType is the third field's even when a tag ahead of it stopped the walk: `FrameHeader`'s
// `fault_ref_msg_type` is the third field's value on any fault-free frame, and the two must agree.
//
// 101-inbound-reject-fidelity D5 (FR-005) supersedes 099 FR-025's choice between 373=13 and 14: the
// repeat walk below (`fixt_header_repeats_msg_type`) reads the whole frame with the route's hooks
// and reports where later 35s stand; refuse_active_frame_ answers from that report.
//
// Internal header: NOT under include/fixpp/. No heap on any path (NFR-002).
#pragma once

#include <cstddef>
#include <cstdint>
#include <fixpp/dict/dictionary.hpp>  // is_fixt_standard_header_tag
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>  // is_fixt_admin_msgtype
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/length_data_carry.hpp>
#include <fixpp/wire/tag_scan.hpp>
#include <span>
#include <string_view>

namespace fixpp::session::detail {

struct fixt_route_peek {
    std::string_view msg_type;  // the third field's value; empty unless `readable`
    std::string_view
        appl_ver_id;           // the first 1128 before the stop; `header_1128` tells it is there
    bool readable = false;     // the third field is 35 and nothing faulted before the stop
    bool admin = false;        // dict::is_fixt_admin_msgtype of `msg_type`
    bool header_1128 = false;  // a 1128 stands before the stop, even an empty one
};

namespace peek_detail {

inline constexpr std::uint16_t kMsgTypeTag = 35;
inline constexpr std::uint16_t kApplVerIdTag = 1128;
inline constexpr std::uint32_t kMsgTypeField = 3;

// Calls `on_field(field_no, tag, value, past_stop)` for each field read, in frame order. Returns
// false when a field it had to read faults. A field ahead of the third is read whatever its tag,
// with `past_stop` set once a tag has stopped the walk, because the third field must still be
// found on the boundaries the scan uses. From the third field on, the walk ends at the stop,
// unless `kWholeFrame` (the repeat walk, 101 D5), which reads every field to the frame's end with
// one carry. `hooks` are the hooks every field is read with; `s_table` classifies the stop.
template <bool kWholeFrame = false, class OnField>
[[nodiscard]] bool walk_fixt_header(std::span<const std::byte> frame,
                                    fixpp::wire::dict_hooks const& hooks,
                                    fixpp::dict::table_view const& s_table,
                                    OnField const& on_field) noexcept {
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    const std::size_t n = frame.size();
    fixpp::wire::length_data_carry carry;
    std::size_t i = 0;
    std::uint32_t field_no = 0;
    bool past_stop = false;
    while (i < n && (kWholeFrame || !past_stop || field_no < kMsgTypeField)) {
        const std::size_t field_start = i;
        ++field_no;
        std::uint32_t tag = 0;
        while (i < n && frame[i] != EQ && frame[i] != SOH) {
            auto const c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9' || !fixpp::wire::accumulate_tag_digit(tag, c)) {
                return false;
            }
            ++i;
        }
        if (i >= n || frame[i] != EQ || i == field_start) {
            return false;
        }
        ++i;  // skip '='
        auto const wire_tag = static_cast<std::uint16_t>(tag);
        past_stop = past_stop || !(s_table.is_header_tag(wire_tag) ||
                                   fixpp::dict::is_fixt_standard_header_tag(wire_tag));
        if constexpr (!kWholeFrame) {
            if (past_stop && field_no > kMsgTypeField) {
                return true;
            }
        }
        std::size_t const vstart = i;
        auto const value = carry.read_value(frame, vstart, wire_tag, hooks);
        if (!value) {
            return false;
        }
        i = value->end;
        on_field(field_no, wire_tag,
                 std::string_view{reinterpret_cast<const char*>(frame.data() + vstart), i - vstart},
                 past_stop);
        if (i < n) {
            ++i;  // skip SOH
        }
    }
    return true;
}

}  // namespace peek_detail

// A frame that is not `readable` routes to S, and its arm's fault and garble dispositions run.
[[nodiscard]] inline fixt_route_peek peek_fixt_route(
    std::span<const std::byte> frame, fixpp::wire::dict_hooks const& s_hooks,
    fixpp::dict::table_view const& s_table) noexcept {
    fixt_route_peek peek;
    bool third_is_35 = false;
    const bool read = peek_detail::walk_fixt_header(
        frame, s_hooks, s_table,
        [&peek, &third_is_35](std::uint32_t field_no, std::uint16_t tag, std::string_view value,
                              bool past_stop) noexcept {
            if (field_no == peek_detail::kMsgTypeField && tag == peek_detail::kMsgTypeTag) {
                third_is_35 = true;
                peek.msg_type = value;
            }
            if (tag == peek_detail::kApplVerIdTag && !past_stop && !peek.header_1128) {
                peek.header_1128 = true;
                peek.appl_ver_id = value;
            }
        });
    if (!read || !third_is_35) {
        return fixt_route_peek{};
    }
    peek.readable = true;
    peek.admin = fixpp::dict::is_fixt_admin_msgtype(peek.msg_type);
    return peek;
}

// What the repeat walk saw: a 35 after the third field standing in the header, and one past it.
// Both false when the walk faulted (the 35s it had seen are discarded) or saw no later 35.
struct msg_type_repeat_report {
    bool in_header = false;
    bool past_header = false;
};

// The cold path of a repeated MsgType. 101-inbound-reject-fidelity D5 (FR-005) supersedes 099
// FR-025's walk here: one walk over the whole frame, which does not end at the header's stop
// (`walk_fixt_header`'s `kWholeFrame` form, so it reads a tag exactly as the peek does). Every
// field is read with `route_hooks` (the hooks the arm's scan_frame_header call used; the owner
// ruling research D5 records), through one `length_data_carry` never reset or replaced, so a Data
// value counted by a header Length is read whole across the stop. Each tag is classified by S's
// header union (`s_table`'s header tags plus the standard header tags); once a field is not a
// header field, every later field is past the header. A 35 after the third field is in the header
// when no field before it was past the header. peek_fixt_route's walk and its stop are unchanged.
[[nodiscard]] inline msg_type_repeat_report fixt_header_repeats_msg_type(
    std::span<const std::byte> frame, fixpp::wire::dict_hooks const& route_hooks,
    fixpp::dict::table_view const& s_table) noexcept {
    msg_type_repeat_report seen;
    const bool read = peek_detail::walk_fixt_header</*kWholeFrame=*/true>(
        frame, route_hooks, s_table,
        [&seen](std::uint32_t field_no, std::uint16_t tag, std::string_view,
                bool past_stop) noexcept {
            if (field_no > peek_detail::kMsgTypeField && tag == peek_detail::kMsgTypeTag) {
                (past_stop ? seen.past_header : seen.in_header) = true;
            }
        });
    return read ? seen : msg_type_repeat_report{};
}

}  // namespace fixpp::session::detail
