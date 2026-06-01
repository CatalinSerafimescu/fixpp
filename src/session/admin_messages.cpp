// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/admin_messages.cpp
//
// fixpp::session admin-message build/interpret out-of-line bodies.
//
// Phase 3 / T021: build_logon / interpret_logon
//   - build_logon: constructs a complete FIX Logon (35=A) frame over wire::Writer.
//     Fields: 8=BeginString, 35=A, 34=seq, 49=SenderCompID, 52=SendingTime,
//             56=TargetCompID, 98=0 (EncryptMethod=None), 108=HeartBtInt.
//   - interpret_logon: parses an inbound Logon frame (streamed via Iter parser),
//     validates BeginString/CompID, extracts HeartBtInt.
//
// Remaining stubs (Phase 5–7):
//   T040 (Phase 5 / US3): build_heartbeat / build_test_request + TestReqID echo.
//   T046 (Phase 6 / US4): build_logout.
//   T054 (Phase 7 / US5): build_reject (RefSeqNum/RefTagID/RefMsgType/
//         SessionRejectReason) + no-reject-loop guard (I-5).
//
// Anchors: data-model.md E5; contracts/admin_messages.hpp; spec FR-002..007;
// [FIX-SL §4.2]–§4.6.
#include <cstddef>
#include <cstdint>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/session/admin_messages.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/wire/writer.hpp>
#include <memory_resource>
#include <span>
#include <string>  // NOLINT(misc-include-cleaner) — IWYU: std::char_traits via string_view ops
#include <string_view>
#include <utility>

namespace fixpp::session {

namespace {

// Render a uint32_t as ASCII decimal into a stack buffer.
// Returns a string_view into `buf` covering the rendered digits.
// buf must be at least 10 chars.
[[nodiscard]] std::string_view render_u32(std::uint32_t v, char* buf, std::size_t n) noexcept {
    if (n == 0) {
        return {};
    }
    // Special case: 0.
    if (v == 0) {
        buf[0] = '0';
        return {buf, 1};
    }
    // Fill from the end.
    std::size_t pos = n;
    while (v > 0 && pos > 0) {
        buf[--pos] = static_cast<char>('0' + static_cast<int>(v % 10U));
        v /= 10U;
    }
    if (v != 0) {
        return {};
    }  // overflow
    return {buf + pos, n - pos};
}

// Convert a string_view to a raw-byte span for wire::Writer::append_raw.
[[nodiscard]] std::span<const std::byte> sv_to_bytes(std::string_view sv) noexcept {
    return {reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
}

}  // namespace

// ── Logon (35=A) ─────────────────────────────────────────────────────────────────
// FR-002/003/004/011, [FIX-SL §4.2]/§4.3. S-001/S-015/S-016/S-020.

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender_comp_id
// / target_comp_id / begin_string); strong typedefs would churn 21 test binaries' call sites.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_logon(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view begin_string, int heartbt_int,
    std::string_view sending_time, bool reset_seqnum) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    // Use std::pmr::null_memory_resource() for group scratch (no groups in Logon).
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());

    // 8=BeginString (first call: also injects the 9= placeholder).
    if (auto r = w.append_raw(8, sv_to_bytes(begin_string)); !r) {
        return std::unexpected(r.error());
    }

    // 35=A (MsgType)
    {
        std::byte val[] = {static_cast<std::byte>('A')};
        if (auto r = w.append_raw(35, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 34=seq (MsgSeqNum)
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(seq), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(34, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 49=SenderCompID
    if (auto r = w.append_raw(49, sv_to_bytes(sender_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 52=SendingTime — use the pre-formatted sending_time from effective_clock.now().
    // FR-003/RC#4: kSendingTimePlaceholder removed; caller supplies the real timestamp.
    {
        if (auto r = w.append_raw(52, sv_to_bytes(sending_time)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 56=TargetCompID
    if (auto r = w.append_raw(56, sv_to_bytes(target_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 98=0 (EncryptMethod=None — required by [FIX-SL §4.2])
    {
        std::byte val[] = {static_cast<std::byte>('0')};
        if (auto r = w.append_raw(98, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 108=HeartBtInt
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(heartbt_int < 0 ? 0 : heartbt_int), nbuf,
                             sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(108, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 141=Y (ResetSeqNumFlag) — emitted only when reset_seqnum=true.
    // RC#C (gate-b/r1): bilateral_strict callers set this to request mutual reset.
    // [spec.md FR-017; Clarifications Q1=A]
    if (reset_seqnum) {
        std::byte val[] = {static_cast<std::byte>('Y')};
        if (auto r = w.append_raw(141, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // Commit: backpatch BodyLength(9=) and append CheckSum(10=).
    auto committed = std::move(w).commit();
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return out.subspan(0, *committed);
}

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender / target
// / begin matches the on-wire field order).
[[nodiscard]] fixpp::core::expected_t<int> interpret_logon(
    std::span<const std::byte> frame, std::string_view expected_sender,
    std::string_view expected_target, std::string_view expected_begin) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    // Parse using the dict-free Iter mode: no heap, no dictionary required.
    // Fields of interest:
    //   8=BeginString, 35=MsgType (must be "A"), 49=SenderCompID, 56=TargetCompID,
    //   108=HeartBtInt.
    // We skip 34, 52, 98 for this validation step.

    // Build a framer view over the raw bytes (no framing validation needed here;
    // the engine's Framer validates checksum/bodylength before on_inbound_frame).
    // Use the Iter parser directly over the raw frame bytes.

    // We parse manually using the Iter MessageView field_iterator so we don't
    // depend on a live framer (frame is already a verified flat byte span).
    // The frame format: 8=...\x019=...\x01<body fields>\x0110=...\x01.

    std::string_view begin_string_found;
    std::string_view msg_type_found;
    std::string_view sender_found;
    std::string_view target_found;
    int heartbt_int_found = -1;
    bool has_heartbt = false;

    // Simple SOH-delimited field scanner (no heap, no library dependency).
    const std::byte SOH{0x01};
    std::size_t i = 0;
    const std::size_t n = frame.size();

    while (i < n) {
        // Parse tag digits.
        std::uint32_t tag = 0;
        while (i < n && frame[i] != static_cast<std::byte>('=') && frame[i] != SOH) {
            auto c = static_cast<unsigned char>(frame[i]);
            if (c < '0' || c > '9') {
                goto next_field;  // NOLINT(cppcoreguidelines-avoid-goto,hicpp-avoid-goto) — manual
                                  // SOH-scanner skip-malformed-field; refactoring to
                                  // nested-flag/break-stack inflates the parser for no behavioral
                                  // win.
            }
            tag = (tag * 10U) + static_cast<std::uint32_t>(c - '0');
            ++i;
        }
        if (i >= n || frame[i] != static_cast<std::byte>('=')) {
            goto next_field;  // NOLINT(cppcoreguidelines-avoid-goto,hicpp-avoid-goto) — same
                              // skip-malformed-field path; see above.
        }
        ++i;  // skip '='

        {
            // Parse value until SOH.
            std::size_t vstart = i;
            while (i < n && frame[i] != SOH) {
                ++i;
            }
            std::string_view val(reinterpret_cast<const char*>(frame.data() + vstart), i - vstart);

            switch (tag) {
                case 8:
                    begin_string_found = val;
                    break;
                case 35:
                    msg_type_found = val;
                    break;
                case 49:
                    sender_found = val;
                    break;
                case 56:
                    target_found = val;
                    break;
                case 108:
                    // Parse HeartBtInt as integer.
                    {
                        int v = 0;
                        for (char ch : val) {
                            if (ch < '0' || ch > '9') {
                                v = -1;
                                break;
                            }
                            v = (v * 10) + static_cast<int>(ch - '0');
                        }
                        heartbt_int_found = v;
                        has_heartbt = true;
                    }
                    break;
                default:
                    break;
            }
        }

        // Skip trailing SOH.
        if (i < n && frame[i] == SOH) {
            ++i;
        }
        continue;

    next_field:
        // Skip to next SOH.
        while (i < n && frame[i] != SOH) {
            ++i;
        }
        if (i < n) {
            ++i;
        }
    }

    // ── Validation ──────────────────────────────────────────────────────────────
    // 1. MsgType must be A (Logon).
    if (msg_type_found != "A") {
        return std::unexpected(fixpp::core::error::session_invalid_logon);
    }

    // 2. BeginString must match expected.
    if (begin_string_found != expected_begin) {
        return std::unexpected(fixpp::core::error::session_begin_string_unsupported);
    }

    // 3. SenderCompID must match expected_sender.
    // (peer's SenderCompID is our TargetCompID's counterparty — i.e. peer sends
    //  "from whom this is" = expected_sender)
    if (sender_found != expected_sender) {
        return std::unexpected(fixpp::core::error::session_compid_mismatch);
    }

    // 4. TargetCompID must match expected_target.
    if (target_found != expected_target) {
        return std::unexpected(fixpp::core::error::session_compid_mismatch);
    }

    // 5. HeartBtInt must be present and ≥ 0.
    if (!has_heartbt || heartbt_int_found < 0) {
        return std::unexpected(fixpp::core::error::session_invalid_logon);
    }

    return heartbt_int_found;
}

// ── Logout (35=5) ────────────────────────────────────────────────────────────────
// FR-005, [FIX-SL §4.6]. S-002.

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender / target
// / text / begin_string / sending_time).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_logout(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view text, std::string_view begin_string,
    std::string_view sending_time) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    // Build a Logout(35=5) frame.
    // Fields: 8=begin_string, 35=5, 34=seq, 49=SenderCompID, 52=sending_time,
    //         56=TargetCompID, [58=text (optional)].
    // FR-002/FR-003/RC#4: begin_string + sending_time threaded through from caller.
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());

    // 8=BeginString — negotiated FIX version from caller (FR-002/RC#4).
    {
        if (auto r = w.append_raw(8, sv_to_bytes(begin_string)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 35=5 (MsgType: Logout)
    {
        std::byte val[] = {static_cast<std::byte>('5')};
        if (auto r = w.append_raw(35, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 34=seq (MsgSeqNum)
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(seq), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(34, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 49=SenderCompID
    if (auto r = w.append_raw(49, sv_to_bytes(sender_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 52=SendingTime — from effective_clock.now() (FR-003/RC#4).
    {
        if (auto r = w.append_raw(52, sv_to_bytes(sending_time)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 56=TargetCompID
    if (auto r = w.append_raw(56, sv_to_bytes(target_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 58=Text (optional — only included when non-empty).
    if (!text.empty()) {
        if (auto r = w.append_raw(58, sv_to_bytes(text)); !r) {
            return std::unexpected(r.error());
        }
    }

    // Commit: backpatch BodyLength(9=) and append CheckSum(10=).
    auto committed = std::move(w).commit();
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return out.subspan(0, *committed);
}

// ── Heartbeat (35=0) ─────────────────────────────────────────────────────────────
// FR-006, [FIX-SL §4.5.1]. S-003.
// T040 (Phase 5 / US3): build Heartbeat(35=0) frame.
// When test_req_id is non-empty, includes TestReqID(112) echoing the value.
// When test_req_id is empty, omits tag 112 ([FIX-SL §4.5.1] — optional field).

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender / target
// / test_req_id / begin_string / sending_time).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_heartbeat(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view test_req_id, std::string_view begin_string,
    std::string_view sending_time) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    // FR-002/FR-003/RC#4: begin_string + sending_time threaded through from caller.
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());

    // 8=BeginString — negotiated FIX version from caller (FR-002/RC#4).
    {
        if (auto r = w.append_raw(8, sv_to_bytes(begin_string)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 35=0 (MsgType: Heartbeat)
    {
        std::byte val[] = {static_cast<std::byte>('0')};
        if (auto r = w.append_raw(35, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 34=seq (MsgSeqNum)
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(seq), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(34, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 49=SenderCompID
    if (auto r = w.append_raw(49, sv_to_bytes(sender_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 52=SendingTime — from effective_clock.now() (FR-003/RC#4).
    {
        if (auto r = w.append_raw(52, sv_to_bytes(sending_time)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 56=TargetCompID
    if (auto r = w.append_raw(56, sv_to_bytes(target_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 112=TestReqID — ONLY emitted when test_req_id is non-empty.
    // [FIX-SL §4.5.1]: the TestReqID field is present in a Heartbeat response
    // to a TestRequest; it is absent in an idle-timer-triggered Heartbeat.
    if (!test_req_id.empty()) {
        if (auto r = w.append_raw(112, sv_to_bytes(test_req_id)); !r) {
            return std::unexpected(r.error());
        }
    }

    auto committed = std::move(w).commit();
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return out.subspan(0, *committed);
}

// ── TestRequest (35=1) ───────────────────────────────────────────────────────────
// FR-006, [FIX-SL §4.5.5]. S-004.
// T040 (Phase 5 / US3): build TestRequest(35=1) frame carrying TestReqID(112).
// test_req_id must be non-empty ([FIX-SL §4.5.5]: "a unique TestReqID").

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender / target
// / test_req_id / begin_string / sending_time).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_test_request(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, std::string_view test_req_id, std::string_view begin_string,
    std::string_view sending_time) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    // FR-002/FR-003/RC#4: begin_string + sending_time threaded through from caller.
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());

    // 8=BeginString — negotiated FIX version from caller (FR-002/RC#4).
    {
        if (auto r = w.append_raw(8, sv_to_bytes(begin_string)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 35=1 (MsgType: TestRequest)
    {
        std::byte val[] = {static_cast<std::byte>('1')};
        if (auto r = w.append_raw(35, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 34=seq (MsgSeqNum)
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(seq), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(34, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 49=SenderCompID
    if (auto r = w.append_raw(49, sv_to_bytes(sender_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 52=SendingTime — from effective_clock.now() (FR-003/RC#4).
    {
        if (auto r = w.append_raw(52, sv_to_bytes(sending_time)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 56=TargetCompID
    if (auto r = w.append_raw(56, sv_to_bytes(target_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 112=TestReqID — REQUIRED for TestRequest ([FIX-SL §4.5.5]).
    if (auto r = w.append_raw(112, sv_to_bytes(test_req_id)); !r) {
        return std::unexpected(r.error());
    }

    auto committed = std::move(w).commit();
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return out.subspan(0, *committed);
}

// ── Reject (35=3) ────────────────────────────────────────────────────────────────
// FR-007, [FIX-SL §4.5.4]. S-007.
// T054 (Phase 7 / US5): build Reject(35=3) with the four reject-specific fields:
//   RefSeqNum(45)            — the offending message's MsgSeqNum
//   RefTagID(371)            — the offending field tag (0 if not applicable → omit)
//   RefMsgType(372)          — the offending message's MsgType (empty → omit)
//   SessionRejectReason(373) — the reject reason code
// Header fields: 8/9/35/49/56/34/52 + trailer 10= per [FIX-SL §4.5.4].
// The no-reject-loop guard (I-5) is at the DISPATCH SITE (Session FSM), not here.
// This builder is dumb: it emits whatever is passed.

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender / target
// before the Ref* group; begin_string / sending_time last).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_reject(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, seqnum_t ref_seq_num, int ref_tag_id,
    std::string_view ref_msg_type, int session_reject_reason, std::string_view begin_string,
    std::string_view sending_time) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    // FR-002/FR-003/RC#4: begin_string + sending_time threaded through from caller.
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());

    // 8=BeginString — negotiated FIX version from caller (FR-002/RC#4).
    {
        if (auto r = w.append_raw(8, sv_to_bytes(begin_string)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 35=3 (MsgType: Session-level Reject)
    {
        std::byte val[] = {static_cast<std::byte>('3')};
        if (auto r = w.append_raw(35, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 34=seq (MsgSeqNum)
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(seq), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(34, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 49=SenderCompID
    if (auto r = w.append_raw(49, sv_to_bytes(sender_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 52=SendingTime — from effective_clock.now() (FR-003/RC#4).
    {
        if (auto r = w.append_raw(52, sv_to_bytes(sending_time)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 56=TargetCompID
    if (auto r = w.append_raw(56, sv_to_bytes(target_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 45=RefSeqNum — the MsgSeqNum of the message being rejected.
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(ref_seq_num), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(45, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 371=RefTagID (optional — only emit when non-zero; 0 = "not applicable").
    if (ref_tag_id > 0) {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(ref_tag_id), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(371, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 372=RefMsgType (optional — only emit when non-empty).
    if (!ref_msg_type.empty()) {
        if (auto r = w.append_raw(372, sv_to_bytes(ref_msg_type)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 373=SessionRejectReason
    {
        char nbuf[12];
        auto sv = render_u32(
            static_cast<std::uint32_t>(session_reject_reason < 0 ? 0 : session_reject_reason), nbuf,
            sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(373, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // Commit: backpatch BodyLength(9=) and append CheckSum(10=).
    auto committed = std::move(w).commit();
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return out.subspan(0, *committed);
}

// ── ResendRequest (35=2) ───────────────────────────────────────────────────────
// FR-009, [FIX-SL §4.3.2]. 013 recovery sub-protocol.
// Fields: 8=begin_string, 35=2, 34=seq, 49=SenderCompID, 52=sending_time,
//         56=TargetCompID, 7=BeginSeqNo, 16=EndSeqNo.

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender / target
// / begin_seqno / end_seqno / begin_string / sending_time).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_resend_request(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, seqnum_t begin_seqno, seqnum_t end_seqno,
    std::string_view begin_string, std::string_view sending_time) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());

    // 8=BeginString — negotiated FIX version from caller (FR-002/RC#4).
    if (auto r = w.append_raw(8, sv_to_bytes(begin_string)); !r) {
        return std::unexpected(r.error());
    }

    // 35=2 (MsgType: ResendRequest)
    {
        std::byte val[] = {static_cast<std::byte>('2')};
        if (auto r = w.append_raw(35, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 34=seq (MsgSeqNum)
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(seq), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(34, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 49=SenderCompID
    if (auto r = w.append_raw(49, sv_to_bytes(sender_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 52=SendingTime — from effective_clock.now() (FR-003/RC#4).
    if (auto r = w.append_raw(52, sv_to_bytes(sending_time)); !r) {
        return std::unexpected(r.error());
    }

    // 56=TargetCompID
    if (auto r = w.append_raw(56, sv_to_bytes(target_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 7=BeginSeqNo
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(begin_seqno), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(7, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 16=EndSeqNo (0 = "through current last outbound" per [FIX-SL §4.3.2]).
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(end_seqno), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(16, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // Commit: backpatch BodyLength(9=) and append CheckSum(10=).
    auto committed = std::move(w).commit();
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return out.subspan(0, *committed);
}

// ── SequenceReset (35=4) — GapFill mode ─────────────────────────────────────────
// FR-009, [FIX-SL §4.4]. 013 recovery sub-protocol (reply to inbound ResendRequest).
// Fields: 8=begin_string, 35=4, 34=seq, 49=SenderCompID, 52=sending_time,
//         56=TargetCompID, 36=NewSeqNo, 123=Y (GapFillFlag).

// NOLINTBEGIN(bugprone-easily-swappable-parameters) — FIX-protocol-fixed arg order (sender / target
// / new_seqno / begin_string / sending_time).
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_sequence_reset_gapfill(
    std::span<std::byte> out, seqnum_t seq, std::string_view sender_comp_id,
    std::string_view target_comp_id, seqnum_t new_seqno, std::string_view begin_string,
    std::string_view sending_time) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    fixpp::wire::Writer w(out, std::pmr::null_memory_resource());

    // 8=BeginString — negotiated FIX version from caller (FR-002/RC#4).
    if (auto r = w.append_raw(8, sv_to_bytes(begin_string)); !r) {
        return std::unexpected(r.error());
    }

    // 35=4 (MsgType: SequenceReset)
    {
        std::byte val[] = {static_cast<std::byte>('4')};
        if (auto r = w.append_raw(35, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // 34=seq (MsgSeqNum)
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(seq), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(34, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 49=SenderCompID
    if (auto r = w.append_raw(49, sv_to_bytes(sender_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 52=SendingTime — from effective_clock.now() (FR-003/RC#4).
    if (auto r = w.append_raw(52, sv_to_bytes(sending_time)); !r) {
        return std::unexpected(r.error());
    }

    // 56=TargetCompID
    if (auto r = w.append_raw(56, sv_to_bytes(target_comp_id)); !r) {
        return std::unexpected(r.error());
    }

    // 36=NewSeqNo
    {
        char nbuf[12];
        auto sv = render_u32(static_cast<std::uint32_t>(new_seqno), nbuf, sizeof(nbuf));
        if (sv.empty()) {
            return std::unexpected(fixpp::core::error::wire_field_value_truncated);
        }
        if (auto r = w.append_raw(36, sv_to_bytes(sv)); !r) {
            return std::unexpected(r.error());
        }
    }

    // 123=Y (GapFillFlag)
    {
        std::byte val[] = {static_cast<std::byte>('Y')};
        if (auto r = w.append_raw(123, std::span<const std::byte>{val}); !r) {
            return std::unexpected(r.error());
        }
    }

    // Commit: backpatch BodyLength(9=) and append CheckSum(10=).
    auto committed = std::move(w).commit();
    if (!committed) {
        return std::unexpected(committed.error());
    }
    return out.subspan(0, *committed);
}

}  // namespace fixpp::session
