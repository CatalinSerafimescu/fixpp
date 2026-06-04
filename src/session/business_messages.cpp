// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/session/business_messages.cpp
//
// 020-g2-business-messages T008 — builder implementations.
//
// Two minimal typed FIX 4.4 application-body builders:
//   build_new_order_single  — 35=D (NewOrderSingle, Limit-only, A-001)
//   build_execution_report  — 35=8 (ExecutionReport, fully-filled, A-006)
//
// Design (contracts/business-messages.md + data-model.md E1/E2 + INV-2/3/4):
//   - Emit the app BODY ONLY: lead with "35=MsgType\x01", then business fields.
//   - NO session header tags (8/9/34/49/52/56) and NO "10=" trailer (INV-2).
//     These are engine-stamped — this is intentional and differs from the admin
//     builders (which use wire::Writer and produce complete frames).
//   - Use a local stack scratch buffer; copy into caller `out` only on full
//     success (INV-4 atomicity). On any failure, return std::unexpected and do
//     NOT touch `out`.
//   - Numeric fields: decimal_t::format(span) for canonical serialisation
//     (FR-007/INV-3, locale-independent, no scientific notation).
//   - Validate fail-closed: empty required string, out-of-range side/enum,
//     unformattable decimal, ill-formed UTCTimestamp, too-small out → error.
//
// Wire::Writer is NOT used here. Writer always injects "8=BeginString\x01 9=...\x01"
// on the first append_raw call and appends "10=CheckSum\x01" at commit(), so it
// has no body-only mode. These builders write raw "tag=value\x01" fields directly.
//
// Anchors: contracts/business-messages.md; data-model.md E1/E2; research.md D3/D4;
//          spec FR-001..008; INV-2/3/4.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/business_messages.hpp>
#include <span>
#include <string_view>

namespace fixpp::session {

namespace {

// SOH byte used as FIX field delimiter.
inline constexpr std::byte kSOH{0x01U};

// Stack scratch buffer size. Must be large enough for the largest possible
// NOS/ExecRpt body. The maximum body is bounded by the sum of all field widths:
//   - 35=D (4) + 11=<cl_ord_id≤64> + 55=<sym≤20> + 54=<1> + 38=<qty≤32> +
//     40=2 (4) + 44=<px≤32> + 60=<tt≤21>
// Well within 512 bytes. 1024 is generous and still stack-safe.
inline constexpr std::size_t kScratchSize = 1024;

// Append "tag=value\x01" into buf[pos..pos+needed-1].
// Returns false if there is insufficient space. Does NOT write on overflow.
[[nodiscard]] bool wfield(std::byte* buf, std::size_t buf_size, std::size_t& pos, std::uint16_t tag,
                          std::string_view value) noexcept {
    // Compute "tag=value\x01" byte count.
    // tag digits: at most 3 chars for tags <= 999 (all FIX tags are <=1000).
    char tag_str[8];
    std::size_t tag_len = 0;
    {
        std::uint16_t t = tag;
        char tmp[8];
        std::size_t tl = 0;
        if (t == 0) {
            tmp[tl++] = '0';
        } else {
            while (t > 0) {
                tmp[tl++] = static_cast<char>('0' + static_cast<int>(t % 10U));
                t /= 10U;
            }
            // Reverse
            for (std::size_t i = 0; i < tl / 2; ++i) {
                char c = tmp[i];
                tmp[i] = tmp[tl - 1 - i];
                tmp[tl - 1 - i] = c;
            }
        }
        tag_len = tl;
        for (std::size_t i = 0; i < tl; ++i) tag_str[i] = tmp[i];
    }

    // Total bytes: tag_len + 1 ('=') + value.size() + 1 (SOH)
    std::size_t needed = tag_len + 1 + value.size() + 1;
    if (pos + needed > buf_size) return false;

    // Write tag digits
    for (std::size_t i = 0; i < tag_len; ++i) {
        buf[pos++] = static_cast<std::byte>(tag_str[i]);
    }
    // '='
    buf[pos++] = static_cast<std::byte>('=');
    // value bytes
    for (char c : value) {
        buf[pos++] = static_cast<std::byte>(c);
    }
    // SOH
    buf[pos++] = kSOH;
    return true;
}

// Append a single-char field "tag=C\x01".
[[nodiscard]] bool wchar(std::byte* buf, std::size_t buf_size, std::size_t& pos, std::uint16_t tag,
                         char ch) noexcept {
    return wfield(buf, buf_size, pos, tag, std::string_view{&ch, 1});
}

// Validate that a char is a printable non-control ASCII character (0x20..0x7E).
// Used for exec_type and ord_status to reject NUL/control bytes.
[[nodiscard]] bool is_printable(char c) noexcept {
    return static_cast<unsigned char>(c) >= 0x20U && static_cast<unsigned char>(c) <= 0x7EU;
}

// Validate that every byte of a string field value is a printable non-control
// ASCII character (0x20..0x7E). Rejects SOH (0x01), NUL (0x00), DEL (0x7F),
// and all other control bytes. This prevents SOH injection via string fields
// (INV-2 / FR-004): a value containing SOH would forge a field boundary inside
// the body, allowing injection of arbitrary FIX tags after the SOH.
// [RC#1: gate-b/r1 business_messages.cpp control-byte sanitization]
[[nodiscard]] bool is_clean_field_value(std::string_view sv) noexcept {
    return std::ranges::all_of(sv, is_printable);
}

// Validate side: only '1' (Buy) or '2' (Sell) are accepted.
[[nodiscard]] bool is_valid_side(char c) noexcept { return c == '1' || c == '2'; }

// Validate UTCTimestamp shape (FR-008).
// Accepts exactly 17 chars ("YYYYMMDD-HH:MM:SS") or 21 chars
// ("YYYYMMDD-HH:MM:SS.sss"). Checks digit/separator positions.
[[nodiscard]] bool is_valid_utc_timestamp(std::string_view sv) noexcept {
    if (sv.size() != 17 && sv.size() != 21) return false;

    // Positions: 01234567 8 9 10 11 12 13 14 15 16
    //            YYYYMMDD - HH :  MM :  SS
    // Position 8 must be '-'
    if (sv[8] != '-') return false;
    // Position 11 must be ':'
    if (sv[11] != ':') return false;
    // Position 14 must be ':'
    if (sv[14] != ':') return false;

    // Digits at positions 0..7, 9..10, 12..13, 15..16
    auto is_digit = [](char c) { return c >= '0' && c <= '9'; };
    for (int i : {0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 12, 13, 15, 16}) {
        if (!is_digit(sv[static_cast<std::size_t>(i)])) return false;
    }

    // For 21-char form: position 17 must be '.', positions 18..20 must be digits
    if (sv.size() == 21) {
        if (sv[17] != '.') return false;
        if (!is_digit(sv[18]) || !is_digit(sv[19]) || !is_digit(sv[20])) return false;
    }

    return true;
}

// Append a decimal_t field into the scratch buffer.
// Uses decimal_t::format(span) for canonical serialization (FR-007).
// Returns false on overflow or format failure.
[[nodiscard]] bool wdecimal(std::byte* buf, std::size_t buf_size, std::size_t& pos,
                            std::uint16_t tag, const fixpp::decimal_t& val) noexcept {
    // Write tag + '=' first, leaving room for the value.
    // Strategy: write tag= into scratch, format decimal into a small local buffer,
    // then write the formatted bytes + SOH.
    char tag_str[8];
    std::size_t tag_len = 0;
    {
        std::uint16_t t = tag;
        char tmp[8];
        std::size_t tl = 0;
        if (t == 0) {
            tmp[tl++] = '0';
        } else {
            while (t > 0) {
                tmp[tl++] = static_cast<char>('0' + static_cast<int>(t % 10U));
                t /= 10U;
            }
            for (std::size_t i = 0; i < tl / 2; ++i) {
                char c = tmp[i];
                tmp[i] = tmp[tl - 1 - i];
                tmp[tl - 1 - i] = c;
            }
        }
        tag_len = tl;
        for (std::size_t i = 0; i < tl; ++i) tag_str[i] = tmp[i];
    }

    // Format the decimal value into a local stack buffer (max 64 bytes for any decimal).
    std::byte val_buf[64];
    auto fmt_r = val.format(std::span<std::byte>{val_buf});
    if (!fmt_r.has_value()) return false;
    std::size_t val_len = *fmt_r;

    // Check capacity: tag_len + 1 + val_len + 1 (SOH)
    std::size_t needed = tag_len + 1 + val_len + 1;
    if (pos + needed > buf_size) return false;

    for (std::size_t i = 0; i < tag_len; ++i) buf[pos++] = static_cast<std::byte>(tag_str[i]);
    buf[pos++] = static_cast<std::byte>('=');
    for (std::size_t i = 0; i < val_len; ++i) buf[pos++] = val_buf[i];
    buf[pos++] = kSOH;
    return true;
}

}  // namespace

// ── NewOrderSingle (35=D) ────────────────────────────────────────────────────
// Field order (contracts/business-messages.md):
//   35=D 11=<cl_ord_id> 55=<symbol> 54=<side> 38=<order_qty> 40=2
//   44=<price> 60=<transact_time>
//
// INV-2: NO 8/9/34/49/52/56 and NO 10= (those are engine-stamped).
// INV-4: build into scratch; copy to `out` only on full success.
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_new_order_single(
    std::span<std::byte> out, std::string_view cl_ord_id, std::string_view symbol, char side,
    const fixpp::decimal_t& order_qty, const fixpp::decimal_t& price,
    std::string_view transact_time) noexcept {
    // Validate required string fields.
    if (cl_ord_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (symbol.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);

    // Reject control bytes (incl. SOH) in string fields to prevent SOH injection
    // (INV-2 / FR-004): a SOH inside cl_ord_id or symbol would forge a field
    // boundary inside the body. [RC#1: gate-b/r1]
    if (!is_clean_field_value(cl_ord_id))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_clean_field_value(symbol))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate side.
    if (!is_valid_side(side))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate transact_time shape (FR-008).
    if (!is_valid_utc_timestamp(transact_time))
        return std::unexpected(fixpp::core::error::wire_invalid_field_format);

    // Build into local stack scratch buffer (INV-4 atomicity).
    std::byte scratch[kScratchSize];
    std::size_t pos = 0;

    // 35=D
    if (!wfield(scratch, kScratchSize, pos, 35, "D"))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 11=<cl_ord_id>
    if (!wfield(scratch, kScratchSize, pos, 11, cl_ord_id))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 55=<symbol>
    if (!wfield(scratch, kScratchSize, pos, 55, symbol))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 54=<side>
    if (!wchar(scratch, kScratchSize, pos, 54, side))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 38=<order_qty>
    if (!wdecimal(scratch, kScratchSize, pos, 38, order_qty)) {
        // wdecimal returns false on both format failure and overflow.
        // Format failure maps to decimal error; overflow maps to frame_too_large.
        // Since we cannot distinguish here, and the scratch is large (1024), overflow
        // is only possible for pathological inputs; treat as format error (FR-007).
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    }
    // 40=2 (OrdType fixed Limit)
    if (!wfield(scratch, kScratchSize, pos, 40, "2"))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 44=<price>
    if (!wdecimal(scratch, kScratchSize, pos, 44, price)) {
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    }
    // 60=<transact_time>
    if (!wfield(scratch, kScratchSize, pos, 60, transact_time))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);

    // Check that caller-supplied `out` is large enough.
    if (out.size() < pos) return std::unexpected(fixpp::core::error::wire_frame_too_large);

    // Full success: copy scratch into out and return the written span.
    for (std::size_t i = 0; i < pos; ++i) out[i] = scratch[i];
    return out.subspan(0, pos);
}

// ── ExecutionReport (35=8) ───────────────────────────────────────────────────
// Field order (contracts/business-messages.md):
//   35=8 37=<order_id> 17=<exec_id> 150=<exec_type> 39=<ord_status>
//   55=<symbol> 54=<side> 151=<leaves_qty> 14=<cum_qty> 6=<avg_px>
[[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> build_execution_report(
    std::span<std::byte> out, std::string_view order_id, std::string_view exec_id, char exec_type,
    char ord_status, std::string_view symbol, char side, const fixpp::decimal_t& leaves_qty,
    const fixpp::decimal_t& cum_qty, const fixpp::decimal_t& avg_px) noexcept {
    // Validate required string fields.
    if (order_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (exec_id.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);
    if (symbol.empty()) return std::unexpected(fixpp::core::error::wire_required_field_missing);

    // Reject control bytes (incl. SOH) in string fields to prevent SOH injection
    // (INV-2 / FR-004). [RC#1: gate-b/r1]
    if (!is_clean_field_value(order_id))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_clean_field_value(exec_id))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_clean_field_value(symbol))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate enum chars: reject NUL and non-printable/control bytes.
    if (!is_printable(exec_type))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);
    if (!is_printable(ord_status))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Validate side.
    if (!is_valid_side(side))
        return std::unexpected(fixpp::core::error::wire_field_value_out_of_range);

    // Build into local stack scratch buffer (INV-4 atomicity).
    std::byte scratch[kScratchSize];
    std::size_t pos = 0;

    // 35=8
    if (!wfield(scratch, kScratchSize, pos, 35, "8"))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 37=<order_id>
    if (!wfield(scratch, kScratchSize, pos, 37, order_id))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 17=<exec_id>
    if (!wfield(scratch, kScratchSize, pos, 17, exec_id))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 150=<exec_type>
    if (!wchar(scratch, kScratchSize, pos, 150, exec_type))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 39=<ord_status>
    if (!wchar(scratch, kScratchSize, pos, 39, ord_status))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 55=<symbol>
    if (!wfield(scratch, kScratchSize, pos, 55, symbol))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 54=<side>
    if (!wchar(scratch, kScratchSize, pos, 54, side))
        return std::unexpected(fixpp::core::error::wire_frame_too_large);
    // 151=<leaves_qty>
    if (!wdecimal(scratch, kScratchSize, pos, 151, leaves_qty)) {
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    }
    // 14=<cum_qty>
    if (!wdecimal(scratch, kScratchSize, pos, 14, cum_qty)) {
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    }
    // 6=<avg_px>
    if (!wdecimal(scratch, kScratchSize, pos, 6, avg_px)) {
        return std::unexpected(fixpp::core::error::decimal_invalid_input);
    }

    // Check that caller-supplied `out` is large enough.
    if (out.size() < pos) return std::unexpected(fixpp::core::error::wire_frame_too_large);

    // Full success: copy scratch into out and return the written span.
    for (std::size_t i = 0; i < pos; ++i) out[i] = scratch[i];
    return out.subspan(0, pos);
}

}  // namespace fixpp::session
