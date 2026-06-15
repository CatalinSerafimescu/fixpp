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
#pragma once

#include <cstddef>
#include <cstdint>
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
    std::string_view begin_seqno;        // tag 7 (BeginSeqNo in ResendRequest)
    std::string_view end_seqno;          // tag 16 (EndSeqNo in ResendRequest)
    std::string_view new_seqno;          // tag 36 (NewSeqNo in SequenceReset)
    std::string_view poss_dup_flag;      // tag 43 (PossDupFlag "Y"/"N")
    std::string_view orig_sending_time;  // tag 122 (OrigSendingTime) — 021 PossDup
    std::string_view gap_fill_flag;      // tag 123 (GapFillFlag in SequenceReset)
    std::string_view reset_seqnum_flag;  // tag 141 (ResetSeqNumFlag in Logon)
    std::string_view
        next_expected_msg_seq_num;  // tag 789 value (may be empty if field present-but-empty) — 027
    bool next_expected_present =
        false;  // tag 789 was present in frame (even if value is empty) — 027
};

[[nodiscard]] inline FrameHeader scan_frame_header(std::span<const std::byte> frame) noexcept {
    FrameHeader h;
    const std::byte SOH{0x01};
    const std::byte EQ{static_cast<std::byte>('=')};
    std::size_t i = 0;
    const std::size_t n = frame.size();

    while (i < n) {
        std::uint32_t tag = 0;
        bool tag_ok = true;
        while (i < n && frame[i] != EQ && frame[i] != SOH) {
            auto c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9') {
                tag_ok = false;
            }
            // Overflow guard: a tag wider than UINT32_MAX would wrap and could
            // ALIAS a known small tag (e.g. 2^32+35 → 35 overrides MsgType).
            // Mark unparseable so the field is skipped, never aliased.
            if (tag > 429496729U) {
                tag_ok = false;
            }
            tag = (tag * 10U) + static_cast<std::uint32_t>(c - '0');
            ++i;
        }
        if (i >= n || frame[i] != EQ || !tag_ok) {
            while (i < n && frame[i] != SOH) {
                ++i;
            }
            if (i < n) {
                ++i;
            }
            continue;
        }
        ++i;  // skip '='
        std::size_t vstart = i;
        while (i < n && frame[i] != SOH) {
            ++i;
        }
        std::string_view val(reinterpret_cast<const char*>(frame.data() + vstart), i - vstart);
        if (i < n) {
            ++i;
        }  // skip SOH

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
                break;
            case 35:
                h.msg_type = val;
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
