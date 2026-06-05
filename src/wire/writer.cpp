// SPDX-License-Identifier: AGPL-3.0-or-later
// src/wire/writer.cpp — fixpp::wire Writer + group_writer out-of-line impl.
// [2b §4.5] Writer::commit() digit-only BodyLength (memmove backpatch) +
// byte-sum-mod-256 CheckSum (3 zero-padded ASCII digits). US2 / T035.
//
// Key constraints:
//   - commit() writes digit-only 9=N (no space padding [FIX50SP2 §3.3]).
//   - CheckSum = byte-sum-mod-256 of ALL bytes from 8= to end of body's SOH,
//     rendered as exactly 3 zero-padded ASCII digits ("10=NNN\x01").
//   - Too-small dst → wire_field_value_truncated; no OOB write.
//   - Zero heap allocation for group-free messages.
//   - (C1) every potentially-throwing trait wrapper call fenced by trap_throw.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/errors.hpp>
#include <fixpp/wire/writer.hpp>
#include <memory_resource>
#include <span>
#include <utility>

namespace fixpp::wire {

namespace {

constexpr std::byte SOH_BYTE{0x01};
constexpr std::byte EQ_BYTE{static_cast<std::byte>('=')};

// Count decimal digits in v (v=0 → 1 digit).
[[nodiscard]] constexpr std::size_t digit_count(std::uint32_t v) noexcept {
    if (v < 10U) {
        return 1;
    }
    if (v < 100U) {
        return 2;
    }
    if (v < 1000U) {
        return 3;
    }
    if (v < 10000U) {
        return 4;
    }
    if (v < 100000U) {
        return 5;
    }
    if (v < 1000000U) {
        return 6;
    }
    if (v < 10000000U) {
        return 7;
    }
    if (v < 100000000U) {
        return 8;
    }
    if (v < 1000000000U) {
        return 9;
    }
    return 10;
}

// Render v as ASCII decimal into buf[0..n). buf must be ≥ digit_count(v) bytes.
// Returns number of bytes written (≥ 1), or 0 if n < digit_count(v).
[[nodiscard]] std::size_t render_u32(std::uint32_t v, std::byte* buf, std::size_t n) noexcept {
    std::size_t dc = digit_count(v);
    if (dc > n) {
        return 0;
    }
    // dc == digit_count(v) ≥ 1, so this writes exactly dc digits least-
    // significant-first and terminates at pos 0 (handles v == 0 → "0").
    std::size_t pos = dc;
    while (pos > 0U) {
        buf[--pos] = static_cast<std::byte>('0' + static_cast<int>(v % 10U));
        v /= 10U;
    }
    return dc;
}

// Write a uint16_t tag as ASCII decimal digits + '=' into dst starting at pos.
// Returns new pos after write, or npos on overflow.
// (buf_size, pos) are positional cursor args of a file-local helper, not a
// public surface; semantically distinct, swap is caught by the round-trip
// property test.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
[[nodiscard]] std::size_t write_tag_eq(std::byte* buf, std::size_t buf_size, std::size_t pos,
                                       std::uint16_t tag) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    // A tag is uint16_t: max value 65535, max 5 digits + '=' = 6 bytes.
    std::array<std::byte, 7> tmp{};
    std::size_t dc = digit_count(static_cast<std::uint32_t>(tag));
    if (pos + dc + 1U > buf_size) {
        return static_cast<std::size_t>(-1);
    }
    std::size_t n = render_u32(static_cast<std::uint32_t>(tag), tmp.data(), tmp.size());
    if (n == 0) {
        return static_cast<std::size_t>(-1);
    }
    std::memcpy(buf + pos, tmp.data(), n);
    pos += n;
    buf[pos++] = EQ_BYTE;
    return pos;
}

}  // namespace

// ── Writer constructor ────────────────────────────────────────────────────────
Writer::Writer(std::span<std::byte> dst, std::pmr::memory_resource* scratch_mr) noexcept
    : dst_{dst}, scratch_mr_{scratch_mr} {}

// ── Writer::write_byte ────────────────────────────────────────────────────────
bool Writer::write_byte(std::byte b) noexcept {
    if (pos_ >= dst_.size()) {
        overflow_ = true;
        return false;
    }
    dst_[pos_++] = b;
    return true;
}

// ── Writer::write_span ────────────────────────────────────────────────────────
bool Writer::write_span(std::span<const std::byte> s) noexcept {
    if (s.size() > dst_.size() - pos_) {
        overflow_ = true;
        return false;
    }
    // An empty value span (e.g. an empty FIX field value "58=\x01") has a null
    // data() pointer; memcpy(dst, nullptr, 0) is UB (src is declared nonnull).
    // Nothing to copy — early-out.
    if (s.empty()) {
        return true;
    }
    std::memcpy(dst_.data() + pos_, s.data(), s.size());
    pos_ += s.size();
    return true;
}

// ── Writer::write_tag_eq ──────────────────────────────────────────────────────
bool Writer::write_tag_eq(std::uint16_t tag) noexcept {
    std::size_t new_pos = ::fixpp::wire::write_tag_eq(dst_.data(), dst_.size(), pos_, tag);
    if (new_pos == npos) {
        overflow_ = true;
        return false;
    }
    pos_ = new_pos;
    return true;
}

// ── Writer::bytes_written ─────────────────────────────────────────────────────
std::size_t Writer::bytes_written() const noexcept { return pos_; }

// ── Writer::append_raw ────────────────────────────────────────────────────────
// Writes "tag=value\x01" into dst.
// On the FIRST call (body_start_ == npos), this is treated as the 8=BeginString
// field. Immediately after writing 8=...\x01, we inject a placeholder 9=NNNNNN\x01
// (6 zero digits = max 999999 ≥ 256 KiB). body_start_ is then recorded at the
// byte immediately following the 9=placeholder\x01 field.
//
// At commit() we memmove the body left to close any over-reservation gap.
core::expected_t<void> Writer::append_raw(std::uint16_t tag,
                                          std::span<const std::byte> value) noexcept {
    if (overflow_) {
        return err_field_value_truncated<void>();
    }

    bool is_first = (body_start_ == npos);

    // Write "tag=value\x01"
    if (!write_tag_eq(tag)) {
        return err_field_value_truncated<void>();
    }
    if (!write_span(value)) {
        return err_field_value_truncated<void>();
    }
    if (!write_byte(SOH_BYTE)) {
        return err_field_value_truncated<void>();
    }

    if (is_first) {
        // Inject the 9=BodyLength placeholder immediately after 8=BeginString.
        // We reserve 6 placeholder digits ('000000') covering up to 999,999 bytes.
        constexpr std::size_t max_bl_digits = 6;

        // Write "9="
        if (!write_byte(std::byte{'9'})) {
            return err_field_value_truncated<void>();
        }
        if (!write_byte(EQ_BYTE)) {
            return err_field_value_truncated<void>();
        }

        // Record position of first placeholder digit.
        bl_digit_pos_ = pos_;
        bl_digit_count_ = max_bl_digits;

        // Write placeholder zeros.
        for (std::size_t k = 0; k < max_bl_digits; ++k) {
            if (!write_byte(std::byte{'0'})) {
                return err_field_value_truncated<void>();
            }
        }
        // Write the SOH that ends the 9= field.
        if (!write_byte(SOH_BYTE)) {
            return err_field_value_truncated<void>();
        }

        // Body starts here.
        body_start_ = pos_;
    }

    return core::expected_t<void>{};
}

// ── Writer::open_group ────────────────────────────────────────────────────────
// (no_tag, count) is the [2b §4.5] Writer::open_group shape oracle — the
// public contract signature; reshaping breaks the extract, so the
// swappable-parameters lint is suppressed with rationale rather than papered.
// NOLINTBEGIN(bugprone-easily-swappable-parameters)
core::expected_t<group_writer> Writer::open_group(std::uint16_t no_tag,
                                                  std::uint32_t count) noexcept {
    // NOLINTEND(bugprone-easily-swappable-parameters)
    std::array<std::byte, 12> count_buf{};
    std::size_t n = render_u32(count, count_buf.data(), count_buf.size());
    if (n == 0) {
        return err_field_value_truncated<group_writer>();
    }
    auto rc = append_raw(no_tag, std::span<const std::byte>{count_buf.data(), n});
    if (!rc) {
        return core::expected_t<group_writer>{std::unexpect, rc.error()};
    }
    return core::expected_t<group_writer>{std::in_place, group_writer_token{}, this};
}

// ── Writer::commit ────────────────────────────────────────────────────────────
// The 9= BodyLength field was reserved at max width; if the actual length needs
// fewer digits, the body is memmove'd left to close the over-reservation gap
// before the digits are patched and the 10=CheckSum field appended.
core::expected_t<std::size_t> Writer::commit() && noexcept {
    if (overflow_) {
        return err_field_value_truncated<std::size_t>();
    }
    if (body_start_ == npos) {
        return err_field_value_truncated<std::size_t>();
    }

    std::size_t body_end = pos_;
    if (body_end < body_start_) {
        return err_field_value_truncated<std::size_t>();
    }
    auto body_length = static_cast<std::uint32_t>(body_end - body_start_);

    // Compute actual digit count for body_length.
    std::size_t actual_digits = digit_count(body_length);
    if (actual_digits > bl_digit_count_) {
        return err_frame_too_large<std::size_t>();
    }

    std::size_t gap = bl_digit_count_ - actual_digits;

    if (gap > 0) {
        // memmove body left by `gap` bytes to close the over-reservation.
        if (body_start_ < gap) {
            return err_field_value_truncated<std::size_t>();
        }
        std::byte* src = dst_.data() + body_start_;
        std::byte* dst_ptr = dst_.data() + body_start_ - gap;
        std::size_t move_sz = body_end - body_start_;
        std::memmove(dst_ptr, src, move_sz);

        body_start_ -= gap;
        body_end -= gap;
        pos_ -= gap;
        bl_digit_count_ = actual_digits;
    }

    // Patch the 9=<digits>\x01 placeholder with the actual body_length.
    // After memmove (if gap > 0), the layout is:
    //   bl_digit_pos_ + 0 .. actual_digits-1: actual digit bytes (to patch)
    //   bl_digit_pos_ + actual_digits        : SOH (terminator of 9= field)
    //   body_start_                          : first body byte
    // We must write both the digits AND the SOH (the old placeholder's extra
    // zeros were overwritten by the memmove; only the new SOH position matters).
    {
        std::byte* digit_ptr = dst_.data() + bl_digit_pos_;
        std::array<std::byte, 10> tmp{};
        std::size_t n = render_u32(body_length, tmp.data(), tmp.size());
        if (n == 0 || n > actual_digits) {
            return err_field_value_truncated<std::size_t>();
        }
        std::memcpy(digit_ptr, tmp.data(), n);
        // Write the SOH that terminates the 9= field.
        digit_ptr[n] = SOH_BYTE;
    }

    // Compute byte-sum-mod-256 over [0, body_end).
    unsigned checksum = 0;
    {
        std::byte const* p = dst_.data();
        for (std::size_t k = 0; k < body_end; ++k) {
            checksum += static_cast<unsigned>(static_cast<unsigned char>(p[k]));
        }
        checksum %= 256U;
    }

    // Write "10=NNN\x01" (7 bytes exactly).
    if (pos_ + 7U > dst_.size()) {
        overflow_ = true;
        return err_field_value_truncated<std::size_t>();
    }
    std::byte* p = dst_.data() + pos_;
    p[0] = std::byte{'1'};
    p[1] = std::byte{'0'};
    p[2] = EQ_BYTE;
    p[3] = static_cast<std::byte>('0' + static_cast<int>(checksum / 100U));
    p[4] = static_cast<std::byte>('0' + static_cast<int>((checksum / 10U) % 10U));
    p[5] = static_cast<std::byte>('0' + static_cast<int>(checksum % 10U));
    p[6] = SOH_BYTE;
    pos_ += 7U;

    return core::expected_t<std::size_t>{pos_};
}

// ── group_writer::close_impl ──────────────────────────────────────────────────
void group_writer::close_impl() noexcept {
    if (closed_ || owner_ == nullptr) {
        return;
    }
    closed_ = true;
    // The count was already written at open_group(); nothing to backpatch.
}

// ── group_writer::append_field ────────────────────────────────────────────────
core::expected_t<void> group_writer::append_field(std::uint16_t tag,
                                                  std::span<const std::byte> value) noexcept {
    if (owner_ == nullptr) {
        return core::expected_t<void>{};
    }
    return owner_->append_raw(tag, value);
}

}  // namespace fixpp::wire
