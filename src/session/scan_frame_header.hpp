// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/scan_frame_header.hpp — internal session header.
//
// FrameHeader struct + scan_frame_header inline free function, extracted from
// the session.cpp anonymous namespace to enable direct unit testing (040 US1
// Phase 3 T005).
//
// Previously defined in session.cpp anonymous namespace; moved here as
// `inline` to allow the test translation unit to include and call the function
// without a separate compiled object. session.cpp includes this header
// (replacing the anonymous-namespace definitions). The test includes it via
// the ${CMAKE_SOURCE_DIR}/src include path added to the test target.
//
// Internal header: NOT under include/fixpp/ (not part of the public API).
// Do NOT include from public headers or library consumers.
//
// Anchors: spec.md FR-003/FR-007; research.md D-3; tasks.md T005/T006.
//
// 092-garbled-frame-reject (specs/092-garbled-frame-reject/data-model.md E-1,
// research.md R-1) supersedes 040's skip-and-continue for a malformed tag: the
// scan now stops at the first field it cannot read and records why in the
// fault members below.
#pragma once

#include <cstddef>
#include <cstdint>
#include <fixpp/wire/dict_hooks.hpp>
#include <fixpp/wire/length_data_carry.hpp>  // fixpp#426: counted Data values
#include <fixpp/wire/tag_scan.hpp>  // 040 US1: accumulate_tag_digit shared bounded-tag helper
#include <span>
#include <string_view>

namespace fixpp::session::detail {

// Minimal SOH-delimited field scanner — no heap, no library.
// Reads tag 8 (BeginString), 34 (MsgSeqNum), 35 (MsgType),
// 49 (SenderCompID), 52 (SendingTime), 56 (TargetCompID), 112 (TestReqID)
// plus 013 Phase 3 tags:
//   7 (BeginSeqNo), 16 (EndSeqNo), 36 (NewSeqNo), 43 (PossDupFlag),
//   123 (GapFillFlag), 141 (ResetSeqNumFlag)
// from a raw FIX frame.
// Used by the inbound dispatch path (scenarios 2i/2k/seqnum check + US3 liveness
// + US5 SendingTime MaxLatency check + T017/T026 recovery).
struct FrameHeader {
    std::string_view begin_string;
    std::string_view sender_comp_id;
    std::string_view target_comp_id;
    std::string_view msg_seq_num;   // tag 34 raw string value
    std::string_view msg_type;      // tag 35 raw string value (T041 US3)
    std::string_view sending_time;  // tag 52 raw string value (T055 US5)
    std::string_view test_req_id;   // tag 112 raw string value (T041 US3)
    // 013 Phase 3 — recovery / reset fields
    std::string_view begin_seqno;             // tag 7 (BeginSeqNo in ResendRequest)
    std::string_view end_seqno;               // tag 16 (EndSeqNo in ResendRequest)
    std::string_view new_seqno;               // tag 36 (NewSeqNo in SequenceReset)
    std::string_view poss_dup_flag;           // tag 43 (PossDupFlag "Y"/"N")
    std::string_view orig_sending_time;       // tag 122 (OrigSendingTime) — 021 PossDup
    std::string_view gap_fill_flag;           // tag 123 (GapFillFlag in SequenceReset)
    std::string_view reset_seqnum_flag;       // tag 141 (ResetSeqNumFlag in Logon)
    std::string_view test_message_indicator;  // tag 464 (TestMessageIndicator) — 070 E5
    std::string_view max_message_size;        // tag 383 (MaxMessageSize) — 070 E5
    std::string_view
        next_expected_msg_seq_num;  // tag 789 value (may be empty if field present-but-empty) — 027
    bool next_expected_present =
        false;  // tag 789 was present in frame (even if value is empty) — 027

    // 092 E-1: the first-fault record and the positional header identification.
    // The members above keep last-wins among the fields read before any fault.
    fixpp::wire::field_fault fault = fixpp::wire::field_fault::none;
    // For length_data_mismatch: the tag of the field right before the faulting
    // Data field (the Length that armed the count). Otherwise 0.
    std::uint16_t fault_length_tag = 0;
    // Offset of the faulting field's first byte. Instruments only: never emitted,
    // never logged.
    std::uint32_t fault_offset = 0;
    // The third field is well-formed and its tag is 35.
    bool msg_type_is_third = false;
    // The value of the FIRST well-formed 34, written once. Read by the disposition
    // only when `fault` is set; also written on a fault-free frame.
    std::string_view fault_ref_seq_num;
    // The third field's value when msg_type_is_third, else empty. Read by the
    // disposition only when `fault` is set; also written on a fault-free frame.
    std::string_view fault_ref_msg_type;
};

// fixpp#426: a Data value counted by its Length is read as one value, so a
// `<SOH>34=` inside EncodedText is not a MsgSeqNum. `hooks` supplies the pairs
// (the session's dictionary, or the standard table alone).
//
// 092 E-1: the scan stops at the first field it cannot read, so every member a
// later field would have set stays empty. A malformed tag (a non-digit byte, a
// tag above 0xFFFF, an empty tag, or no '=' before SOH or the end of the buffer)
// records `malformed_tag`; a count that runs past the frame, reaches its end, or
// is not followed by SOH records `length_data_mismatch` (fixpp#426 design §4).
[[nodiscard]] inline FrameHeader scan_frame_header(
    std::span<const std::byte> frame,
    fixpp::wire::dict_hooks const& hooks = fixpp::wire::dict_hooks::none()) noexcept {
    FrameHeader h;
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    std::size_t i = 0;
    const std::size_t n = frame.size();
    fixpp::wire::length_data_carry carry;
    std::uint16_t prev_tag = 0;  // the previous field's tag: a count's Length
    std::uint32_t field_no = 0;  // 1-based position of the field being read
    bool seen_34 = false;        // fault_ref_seq_num holds the first 34 once set

    // Every return names `h`, so the result is constructed in place on every path.
    const auto record_fault = [&h](fixpp::wire::field_fault kind,
                                   std::size_t field_start) noexcept {
        h.fault = kind;
        h.fault_offset = static_cast<std::uint32_t>(field_start);
    };

    while (i < n) {
        const std::size_t field_start = i;
        ++field_no;
        std::uint32_t tag = 0;
        while (i < n && frame[i] != EQ && frame[i] != SOH) {
            auto c = static_cast<unsigned char>(frame[i]);
            // 040 US1 (FR-002/FR-003/FR-007a): the digit-class clause is FIRST so
            // short-circuit only ever calls accumulate_tag_digit on a '0'..'9' digit (its
            // precondition). A non-digit (e.g. "3a5=") OR an out-of-range tag (>0xFFFF)
            // is a malformed tag, never dispatched under an aliased tag. Do NOT drop the
            // digit-class clause and lean on the helper — the non-digit witness (FR-007a /
            // research.md D-3) guards against exactly that.
            if (c < '0' || c > '9' || !fixpp::wire::accumulate_tag_digit(tag, c)) {
                record_fault(fixpp::wire::field_fault::malformed_tag, field_start);
                return h;
            }
            ++i;
        }
        if (i >= n || frame[i] != EQ || i == field_start) {
            record_fault(fixpp::wire::field_fault::malformed_tag, field_start);
            return h;
        }
        ++i;  // skip '='
        std::size_t vstart = i;
        auto const value = carry.read_value(frame, vstart, static_cast<std::uint16_t>(tag), hooks);
        if (!value) {
            h.fault_length_tag = prev_tag;
            record_fault(fixpp::wire::field_fault::length_data_mismatch, field_start);
            return h;
        }
        i = value->end;
        std::string_view val(reinterpret_cast<const char*>(frame.data() + vstart), i - vstart);
        if (i < n) {
            ++i;
        }  // skip SOH
        prev_tag = static_cast<std::uint16_t>(tag);

        switch (tag) {
            case 7:
                h.begin_seqno = val;
                break;  // T026 ResendRequest
            case 8:
                h.begin_string = val;
                break;
            case 16:
                h.end_seqno = val;
                break;  // T026 ResendRequest
            case 34:
                h.msg_seq_num = val;
                if (!seen_34) {
                    h.fault_ref_seq_num = val;
                    seen_34 = true;
                }
                break;
            case 35:
                h.msg_type = val;
                if (field_no == 3) {
                    h.msg_type_is_third = true;
                    h.fault_ref_msg_type = val;
                }
                break;  // T041 US3
            case 36:
                h.new_seqno = val;
                break;  // T026 SequenceReset
            case 43:
                h.poss_dup_flag = val;
                break;  // T026 PossDupFlag
            case 49:
                h.sender_comp_id = val;
                break;
            case 52:
                h.sending_time = val;
                break;  // T055 US5 SendingTime
            case 56:
                h.target_comp_id = val;
                break;
            case 112:
                h.test_req_id = val;
                break;  // T041 US3
            case 122:
                h.orig_sending_time = val;
                break;  // 021 PossDup OrigSendingTime
            case 123:
                h.gap_fill_flag = val;
                break;  // T026 SequenceReset GapFillFlag
            case 141:
                h.reset_seqnum_flag = val;
                break;  // T027 ResetSeqNumFlag
            case 383:
                h.max_message_size = val;
                break;  // 070 E5 MaxMessageSize
            case 464:
                h.test_message_indicator = val;
                break;  // 070 E5 TestMessageIndicator
            case 789:
                h.next_expected_msg_seq_num = val;
                h.next_expected_present =
                    true;  // set even when val is empty (D-10 empty-value guard)
                break;     // 027 NextExpectedMsgSeqNum in Logon
            default:
                break;
        }
    }
    return h;
}

}  // namespace fixpp::session::detail
