// SPDX-License-Identifier: AGPL-3.0-or-later
// src/wire/framer.cpp — fixpp::wire out-of-line implementation.
// US3 / T040 framing algorithm.

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/errors.hpp>
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/view.hpp>
#include <span>

namespace fixpp::wire {
namespace {

constexpr std::byte soh_byte{0x01};

struct parsed_frame {
    enum class status : std::uint8_t {
        complete,
        partial,
        error,
    };

    status status_code = status::partial;
    std::size_t frame_len = 0;
    std::size_t body_off = 0;
    std::size_t body_len = 0;
    core::error error_code = core::error::wire_invalid_body_length;
};

[[nodiscard]] constexpr bool is_digit(std::byte b) noexcept {
    auto const ch = static_cast<unsigned char>(b);
    return ch >= static_cast<unsigned char>('0') && ch <= static_cast<unsigned char>('9');
}

[[nodiscard]] parsed_frame make_error(core::error code) noexcept {
    return parsed_frame{
        .status_code = parsed_frame::status::error,
        .frame_len = 0,
        .body_off = 0,
        .body_len = 0,
        .error_code = code,
    };
}

[[nodiscard]] parsed_frame make_partial() noexcept {
    return parsed_frame{
        .status_code = parsed_frame::status::partial,
    };
}

[[nodiscard]] parsed_frame make_complete(std::size_t frame_len, std::size_t body_off,
                                         std::size_t body_len) noexcept {
    return parsed_frame{
        .status_code = parsed_frame::status::complete,
        .frame_len = frame_len,
        .body_off = body_off,
        .body_len = body_len,
    };
}

[[nodiscard]] std::size_t find_soh(std::span<const std::byte> bytes, std::size_t start) noexcept {
    for (std::size_t i = start; i < bytes.size(); ++i) {
        if (bytes[i] == soh_byte) {
            return i;
        }
    }
    return bytes.size();
}

[[nodiscard]] parsed_frame parse_frame(std::span<const std::byte> bytes,
                                       std::size_t max_frame_bytes) noexcept {
    if (bytes.empty()) {
        return make_partial();
    }
    if (bytes[0] != std::byte{'8'}) {
        return make_error(core::error::wire_framing_resync);
    }
    if (bytes.size() < 2U) {
        return make_partial();
    }
    if (bytes[1] != std::byte{'='}) {
        return make_error(core::error::wire_framing_resync);
    }

    std::size_t const beginstring_end = find_soh(bytes, 2U);
    if (beginstring_end == bytes.size()) {
        return make_partial();
    }

    std::size_t const body_length_tag = beginstring_end + 1U;
    if (body_length_tag >= bytes.size()) {
        return make_partial();
    }
    if (body_length_tag + 1U >= bytes.size()) {
        return make_partial();
    }
    if (bytes[body_length_tag] != std::byte{'9'} || bytes[body_length_tag + 1U] != std::byte{'='}) {
        return make_error(core::error::wire_invalid_body_length);
    }

    std::size_t const body_length_value = body_length_tag + 2U;
    std::size_t const body_length_end = find_soh(bytes, body_length_value);
    if (body_length_end == bytes.size()) {
        return make_partial();
    }
    if (body_length_end == body_length_value) {
        return make_error(core::error::wire_invalid_body_length);
    }

    std::size_t body_length = 0;
    for (std::size_t i = body_length_value; i < body_length_end; ++i) {
        if (!is_digit(bytes[i])) {
            return make_error(core::error::wire_invalid_body_length);
        }
        std::size_t const digit = static_cast<std::size_t>(static_cast<unsigned char>(bytes[i])) -
                                  static_cast<std::size_t>('0');
        if (body_length > ((max_frame_bytes - digit) / static_cast<std::size_t>(10))) {
            return make_error(core::error::wire_frame_too_large);
        }
        body_length = (body_length * static_cast<std::size_t>(10)) + digit;
    }

    if (body_length == 0U) {
        return make_error(core::error::wire_invalid_body_length);
    }
    if (body_length > max_frame_bytes) {
        return make_error(core::error::wire_frame_too_large);
    }

    std::size_t const body_off = body_length_end + 1U;
    if (body_off > bytes.size()) {
        return make_partial();
    }
    if (body_off + body_length > bytes.size()) {
        return make_partial();
    }

    std::size_t const checksum_off = body_off + body_length;
    if (checksum_off + 7U > bytes.size()) {
        return make_partial();
    }
    if (bytes[checksum_off] != std::byte{'1'} || bytes[checksum_off + 1U] != std::byte{'0'} ||
        bytes[checksum_off + 2U] != std::byte{'='}) {
        return make_error(core::error::wire_invalid_body_length);
    }
    if (bytes[body_off + body_length - 1U] != soh_byte) {
        return make_error(core::error::wire_invalid_body_length);
    }

    std::array<unsigned, 3> checksum_digits{};
    for (std::size_t i = 0; i < checksum_digits.size(); ++i) {
        std::byte const digit = bytes[checksum_off + 3U + i];
        if (!is_digit(digit)) {
            return make_error(core::error::wire_checksum_mismatch);
        }
        checksum_digits[i] =
            static_cast<unsigned>(static_cast<unsigned char>(digit)) - static_cast<unsigned>('0');
    }
    if (bytes[checksum_off + 6U] != soh_byte) {
        return make_error(core::error::wire_checksum_mismatch);
    }

    unsigned checksum = 0;
    for (std::size_t i = 0; i < checksum_off; ++i) {
        checksum += static_cast<unsigned>(static_cast<unsigned char>(bytes[i]));
    }
    checksum %= 256U;

    unsigned const encoded_checksum =
        (checksum_digits[0] * 100U) + (checksum_digits[1] * 10U) + checksum_digits[2];
    if (encoded_checksum != checksum) {
        return make_error(core::error::wire_checksum_mismatch);
    }

    std::size_t const frame_len = checksum_off + 7U;
    if (frame_len > max_frame_bytes) {
        return make_error(core::error::wire_frame_too_large);
    }

    return make_complete(frame_len, body_off, body_length);
}

// ── 093 resync mode (contract C-1; data-model E-1) ───────────────────────────
// Everything below runs only with Config::resync_on_garble set. The strict path
// above is untouched, the order of its frame_len check included (plan OD-4).

// Bytes the resync path read and summed during one feed (quickstart §2's counted
// work). Each count is taken from the loop that does the work.
struct resync_work {
    std::uint64_t read = 0;
    std::uint64_t summed = 0;
};

struct resync_candidate {
    enum class status : std::uint8_t {
        frame,
        partial,
        garble,
        too_large,
    };

    status status_code = status::partial;
    core::error kind{};         // a garble's kind
    bool through_end = false;   // a structurally complete frame with a wrong CheckSum
    std::size_t frame_len = 0;  // set for a frame, and for a garble with through_end
    std::size_t body_off = 0;
    std::size_t body_len = 0;
};

[[nodiscard]] resync_candidate resync_garble(core::error kind) noexcept {
    return resync_candidate{.status_code = resync_candidate::status::garble, .kind = kind};
}

[[nodiscard]] resync_candidate resync_partial() noexcept {
    return resync_candidate{.status_code = resync_candidate::status::partial};
}

[[nodiscard]] resync_candidate resync_too_large() noexcept {
    return resync_candidate{.status_code = resync_candidate::status::too_large};
}

// Frames the candidate at the start of `bytes` (C-1, Outcome at a candidate frame
// start). Both header scans are capped over encoded bytes (W-2), the frame length is
// refused as over max as soon as the body offset is known (OD-4), and the CheckSum is
// summed only over a structurally complete candidate (W-3).
// Its trailer and CheckSum checks repeat parse_frame's on purpose: the strict path's
// check order is fixed byte for byte by plan OD-4, so the shared logic is not
// extracted from it, and only is_digit and soh_byte are shared.
[[nodiscard]] resync_candidate parse_resync_candidate(std::span<const std::byte> bytes,
                                                      std::size_t max_frame_bytes,
                                                      std::size_t max_begin_string_bytes,
                                                      resync_work& work) noexcept {
    std::size_t const n = bytes.size();
    if (n == 0U) {
        return resync_partial();
    }
    ++work.read;
    if (bytes[0] != std::byte{'8'}) {
        return resync_garble(core::error::wire_framing_resync);
    }
    if (n < 2U) {
        return resync_partial();
    }
    ++work.read;
    if (bytes[1] != std::byte{'='}) {
        return resync_garble(core::error::wire_framing_resync);
    }

    // The BeginString value: at most max_begin_string_bytes before its SOH.
    std::size_t const last_soh_at = 2U + max_begin_string_bytes;
    std::size_t beginstring_end = 2U;
    for (; beginstring_end < n; ++beginstring_end) {
        ++work.read;
        if (bytes[beginstring_end] == soh_byte) {
            break;
        }
        if (beginstring_end == last_soh_at) {
            return resync_garble(core::error::wire_framing_resync);
        }
    }
    if (beginstring_end == n) {
        return resync_partial();
    }

    std::size_t const body_length_tag = beginstring_end + 1U;
    if (body_length_tag + 1U >= n) {
        return resync_partial();
    }
    work.read += 2U;
    if (bytes[body_length_tag] != std::byte{'9'} || bytes[body_length_tag + 1U] != std::byte{'='}) {
        return resync_garble(core::error::wire_invalid_body_length);
    }

    // The BodyLength digits: at most kBodyLengthDigitCap of them, refused as over max
    // as soon as the value exceeds it.
    std::size_t const body_length_value = body_length_tag + 2U;
    std::size_t body_length_end = body_length_value;
    std::size_t body_length = 0;
    for (;; ++body_length_end) {
        if (body_length_end == n) {
            return resync_partial();
        }
        ++work.read;
        std::byte const b = bytes[body_length_end];
        if (b == soh_byte) {
            break;
        }
        if (!is_digit(b)) {
            return resync_garble(core::error::wire_invalid_body_length);
        }
        if (body_length_end - body_length_value == Framer::kBodyLengthDigitCap) {
            return resync_garble(core::error::wire_invalid_body_length);
        }
        std::size_t const digit =
            static_cast<std::size_t>(static_cast<unsigned char>(b)) - static_cast<std::size_t>('0');
        if (digit > max_frame_bytes ||
            body_length > ((max_frame_bytes - digit) / static_cast<std::size_t>(10))) {
            return resync_too_large();
        }
        body_length = (body_length * static_cast<std::size_t>(10)) + digit;
    }
    if (body_length_end == body_length_value || body_length == 0U) {
        return resync_garble(core::error::wire_invalid_body_length);
    }

    std::size_t const body_off = body_length_end + 1U;
    std::size_t const checksum_off = body_off + body_length;
    std::size_t const frame_len = checksum_off + 7U;
    if (frame_len > max_frame_bytes) {
        return resync_too_large();
    }
    if (frame_len > n) {
        return resync_partial();
    }

    work.read += 3U;
    if (bytes[checksum_off] != std::byte{'1'} || bytes[checksum_off + 1U] != std::byte{'0'} ||
        bytes[checksum_off + 2U] != std::byte{'='}) {
        return resync_garble(core::error::wire_invalid_body_length);
    }
    ++work.read;
    if (bytes[checksum_off - 1U] != soh_byte) {
        return resync_garble(core::error::wire_invalid_body_length);
    }
    std::array<unsigned, 3> checksum_digits{};
    for (std::size_t i = 0; i < checksum_digits.size(); ++i) {
        ++work.read;
        std::byte const digit = bytes[checksum_off + 3U + i];
        if (!is_digit(digit)) {
            return resync_garble(core::error::wire_checksum_mismatch);
        }
        checksum_digits[i] =
            static_cast<unsigned>(static_cast<unsigned char>(digit)) - static_cast<unsigned>('0');
    }
    ++work.read;
    if (bytes[checksum_off + 6U] != soh_byte) {
        return resync_garble(core::error::wire_checksum_mismatch);
    }

    // Structurally complete: summed once, then consumed whole either way (W-3).
    unsigned checksum = 0;
    std::size_t summed = 0;
    for (; summed < checksum_off; ++summed) {
        checksum += static_cast<unsigned>(static_cast<unsigned char>(bytes[summed]));
    }
    work.summed += summed;
    checksum %= 256U;

    unsigned const encoded_checksum =
        (checksum_digits[0] * 100U) + (checksum_digits[1] * 10U) + checksum_digits[2];
    if (encoded_checksum != checksum) {
        return resync_candidate{.status_code = resync_candidate::status::garble,
                                .kind = core::error::wire_checksum_mismatch,
                                .through_end = true,
                                .frame_len = frame_len};
    }
    return resync_candidate{.status_code = resync_candidate::status::frame,
                            .frame_len = frame_len,
                            .body_off = body_off,
                            .body_len = body_length};
}

constexpr std::array<std::byte, 5> resync_start{std::byte{'8'}, std::byte{'='}, std::byte{'F'},
                                                std::byte{'I'}, std::byte{'X'}};
constexpr std::size_t no_resync_start = static_cast<std::size_t>(-1);

// The offset of the next "8=FIX" in `bytes` at or after `from`, or no_resync_start.
// Then `held` is the length of a trailing proper prefix of "8=FIX", which the carry
// keeps for the next feed (C-1, State across feeds), else 0.
[[nodiscard]] std::size_t find_resync_start(std::span<const std::byte> bytes, std::size_t from,
                                            std::size_t& held, resync_work& work) noexcept {
    std::size_t const n = bytes.size();
    for (std::size_t i = from; i < n; ++i) {
        ++work.read;
        if (bytes[i] != resync_start[0]) {
            continue;
        }
        std::size_t matched = 1;
        for (; matched < resync_start.size() && i + matched < n; ++matched) {
            ++work.read;
            if (bytes[i + matched] != resync_start[matched]) {
                break;
            }
        }
        if (matched == resync_start.size()) {
            held = 0;
            return i;
        }
        if (i + matched == n) {
            held = n - i;
            return no_resync_start;
        }
    }
    held = 0;
    return no_resync_start;
}

}  // namespace

core::expected_t<std::span<frame_view>> Framer::feed_resync_(std::span<const std::byte> incoming,
                                                             pmr_carry_buffer& carry,
                                                             std::span<frame_view> out) noexcept {
    garbles_ = {};

    // The counted work of this call reaches the members on every exit, refusals
    // included: a return that skipped it would undercount, which fails toward clean.
    struct work_flush {
        Framer& framer;
        resync_work work{};
        explicit work_flush(Framer& f) noexcept : framer{f} {}
        work_flush(work_flush const&) = delete;
        work_flush& operator=(work_flush const&) = delete;
        ~work_flush() {
            framer.work_read_ += work.read;
            framer.work_summed_ += work.summed;
        }
    } flush{*this};
    resync_work& work = flush.work;

    // W-1: erase the consumed prefix only when the incoming bytes would not fit after
    // what the carry holds, so a carry-only feed never moves bytes. With nothing
    // pending the carry is emptied, which moves nothing.
    if (pending_ == 0U) {
        carry.clear();
    } else if (pending_ < carry.size() && carry.size() + incoming.size() > carry.capacity()) {
        carry.consume_front(carry.size() - pending_);
        work_moved_ += pending_;
    }

    auto fail_too_large = [&]() noexcept {
        carry.clear();
        pending_ = 0;
        searching_ = false;
        return fail<std::span<frame_view>>(core::error::wire_frame_too_large);
    };

    std::span<const std::byte> source = incoming;
    bool using_carry = false;
    if (!carry.empty()) {
        if (!carry.append(incoming)) {
            return fail_too_large();
        }
        work_moved_ += incoming.size();
        source = carry.bytes().subspan(carry.size() - pending_ - incoming.size());
        using_carry = true;
    }

    std::size_t offset = 0;
    std::size_t produced = 0;
    while (offset < source.size() && produced < out.size()) {
        if (searching_) {
            std::size_t held = 0;
            std::size_t const start = find_resync_start(source, offset, held, work);
            if (start == no_resync_start) {
                garbles_.discarded += source.size() - held - offset;
                offset = source.size() - held;
                break;
            }
            garbles_.discarded += start - offset;
            offset = start;
            searching_ = false;
        }

        resync_candidate const c = parse_resync_candidate(
            source.subspan(offset), cfg_.max_frame_bytes, cfg_.max_begin_string_bytes, work);
        if (c.status_code == resync_candidate::status::partial) {
            break;
        }
        if (c.status_code == resync_candidate::status::frame) {
#ifndef NDEBUG
            auto const gen_tok = detail::current_pool_token(pool_id_);
#else
            detail::generation_token const gen_tok{};
#endif
            out[produced] = frame_view{
                source.data() + offset, c.frame_len, c.body_off, c.body_len, gen_tok,
            };
            ++produced;
            offset += c.frame_len;
            continue;
        }
        // Ordering: a call that produced a frame stops before resolving what follows,
        // so the garbles a call reports precede every frame it produces, and a frame
        // already produced is never discarded by a later refusal.
        if (produced > 0U) {
            break;
        }
        if (c.status_code == resync_candidate::status::too_large) {
            return fail_too_large();
        }
        // A garble opens a region. A wrong-CheckSum frame is consumed through its own
        // end; any other is searched past from the byte after its first byte. Either
        // way the next byte is a search position (C-1, Frame start and Extent).
        if (garbles_.regions == 0U) {
            garbles_.first_kind = c.kind;
        }
        ++garbles_.regions;
        std::size_t const skip = c.through_end ? c.frame_len : std::size_t{1};
        garbles_.discarded += skip;
        offset += skip;
        searching_ = true;
    }

    std::size_t const trailing = source.size() - offset;
    if (using_carry) {
        pending_ = trailing;
        return out.first(produced);
    }

    pending_ = 0;
    if (trailing != 0U) {
        if (!carry.append(source.subspan(offset))) {
            return fail_too_large();
        }
        work_moved_ += trailing;
        pending_ = trailing;
    }
    return out.first(produced);
}

core::expected_t<std::span<frame_view>> Framer::feed(std::span<const std::byte> incoming,
                                                     pmr_carry_buffer& carry,
                                                     std::span<frame_view> out) noexcept {
    if (cfg_.resync_on_garble) {
        return feed_resync_(incoming, carry, out);
    }
    if (pending_ < carry.size()) {
        carry.consume_front(carry.size() - pending_);
    }

    std::span<const std::byte> source = incoming;
    bool using_carry = false;
    if (!carry.empty()) {
        if (!carry.append(incoming)) {
            carry.clear();
            pending_ = 0;
            return fail<std::span<frame_view>>(core::error::wire_frame_too_large);
        }
        source = carry.bytes();
        using_carry = true;
    }

    std::size_t offset = 0;
    std::size_t produced = 0;
    while (offset < source.size()) {
        if (produced == out.size()) {
            break;
        }

        parsed_frame const frame = parse_frame(source.subspan(offset), cfg_.max_frame_bytes);
        if (frame.status_code == parsed_frame::status::partial) {
            break;
        }
        if (frame.status_code == parsed_frame::status::error) {
            carry.clear();
            pending_ = 0;
            return fail<std::span<frame_view>>(frame.error_code);
        }

        std::byte const* frame_ptr = source.data() + offset;
#ifndef NDEBUG
        auto const gen_tok = detail::current_pool_token(pool_id_);
#else
        detail::generation_token const gen_tok{};
#endif
        out[produced] = frame_view{
            frame_ptr, frame.frame_len, frame.body_off, frame.body_len, gen_tok,
        };
        ++produced;
        offset += frame.frame_len;
    }

    std::size_t const trailing = source.size() - offset;
    if (using_carry) {
        pending_ = trailing;
        return out.first(produced);
    }

    carry.clear();
    pending_ = 0;
    if (trailing != 0U) {
        if (!carry.append(source.subspan(offset))) {
            return fail<std::span<frame_view>>(core::error::wire_frame_too_large);
        }
        pending_ = trailing;
    }

    return out.first(produced);
}

}  // namespace fixpp::wire
