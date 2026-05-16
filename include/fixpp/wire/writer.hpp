#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/writer.hpp
// [2b §4.5] Writer + group_writer — serialize a FIX message into a caller-
// supplied buffer with auto-computed digit-only BodyLength (tag 9) and
// byte-sum-mod-256 CheckSum (tag 10) at commit().
//
// Authority: .specify/2b-wire.md v0.2; shape oracle:
//   specs/004-wire-codec/contracts/writer.hpp
//
// Design summary:
//   - Writer(dst, scratch_mr): aliases dst; scratch_mr for group bookkeeping.
//   - append_raw(tag, value_bytes): write "tag=value\x01" into dst.
//   - append<T>(tag, v): delegate to the field's trait to_chars into dst.
//   - open_group(no_tag, count): write "no_tag=count\x01", return group_writer.
//   - commit() &&: backpatch digit-only 9=N and append 10=NNN\x01.
//   - bytes_written(): bytes consumed so far (before commit).
//
// Zero-allocation for group-free messages (no heap between append and commit).
// (C1) every potentially-throwing trait call is fenced by core::detail::trap_throw
//      so no exception escapes the noexcept boundary (FR-013, [arch §5.3]).

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>                        // std::unexpect
#include <fixpp/core/decimal_helpers.hpp>  // core::detail::trap_throw (C1)
#include <fixpp/core/error.hpp>
#include <memory_resource>
#include <span>

namespace fixpp::wire {

class Writer;

// Passkey token: only Writer can create a group_writer_token, ensuring
// group_writer is only constructed through Writer::open_group.
struct group_writer_token {
private:
    friend class Writer;
    group_writer_token() noexcept = default;
};

// ── group_writer ──────────────────────────────────────────────────────────────
// Returned by Writer::open_group; appends fields for one group instance.
// RAII: destructor calls close() if not already closed (seals NoXxx count).
// Must be closed (in LIFO order) before the owning Writer::commit().
class group_writer {
public:
    // Constructed only through the passkey from Writer::open_group.
    explicit group_writer(group_writer_token /*passkey*/, Writer* owner) noexcept : owner_{owner} {}

    group_writer(group_writer&&) noexcept = default;
    group_writer& operator=(group_writer&&) noexcept = default;

    // Copy disallowed — group_writer is a move-only handle.
    group_writer(group_writer const&) = delete;
    group_writer& operator=(group_writer const&) = delete;

    ~group_writer() noexcept { close_impl(); }

    // Append a raw (tag, value bytes) field into the group body.
    [[nodiscard]] core::expected_t<void> append_field(std::uint16_t tag,
                                                      std::span<const std::byte> value) noexcept;

    // Typed convenience: calls the field's trait to_chars.
    template <class T>
    [[nodiscard]] core::expected_t<void> append_field(std::uint16_t tag, T const& v) noexcept;

    // Seal the group (RAII fallback): no-op if already closed.
    void close() && noexcept { close_impl(); }

private:
    friend class Writer;

    void close_impl() noexcept;

    Writer* owner_ = nullptr;
    bool closed_ = false;
};

// ── Writer ────────────────────────────────────────────────────────────────────
class Writer {
public:
    // Alias dst (caller-owned buffer); capture scratch_mr for group bookkeeping.
    // Both must outlive the Writer.
    explicit Writer(std::span<std::byte> dst [[clang::lifetimebound]],
                    std::pmr::memory_resource* scratch_mr [[clang::lifetimebound]]) noexcept;

    // Raw bytes append: writes "tag=value\x01" into dst.
    // Returns wire_field_value_truncated if dst is too small (no OOB write).
    [[nodiscard]] core::expected_t<void> append_raw(std::uint16_t tag,
                                                    std::span<const std::byte> value) noexcept;

    // Typed append: 2a decimal format()->to_chars, then delegates to append_raw.
    // (C1) any potentially-throwing trait call is fenced by trap_throw.
    template <class T>
    [[nodiscard]] core::expected_t<void> append(std::uint16_t tag, T const& v) noexcept;

    // Repeating-group: write "no_tag=count\x01" and return a group_writer that
    // aliases *this. Nesting must close in LIFO order (last opened, first closed).
    [[nodiscard]] core::expected_t<group_writer> open_group(std::uint16_t no_tag,
                                                            std::uint32_t count) noexcept
        [[clang::lifetimebound]];

    // Commit: backpatch digit-only 9=N, append 10=NNN\x01; returns total bytes.
    // The Writer is consumed (move-qualified); calls after commit are UB.
    [[nodiscard]] core::expected_t<std::size_t> commit() && noexcept;

    // Bytes written so far (before commit; excludes the final 9= / 10= fields).
    [[nodiscard]] std::size_t bytes_written() const noexcept;

private:
    friend class group_writer;

    // Write a single raw byte; returns false if dst is full.
    [[nodiscard]] bool write_byte(std::byte b) noexcept;

    // Write raw bytes span; returns false on overflow.
    [[nodiscard]] bool write_span(std::span<const std::byte> s) noexcept;

    // Write tag digits + '=' ; returns false on overflow.
    [[nodiscard]] bool write_tag_eq(std::uint16_t tag) noexcept;

    // The destination buffer.
    std::span<std::byte> dst_;
    // Current write position within dst_.
    std::size_t pos_ = 0;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    // Position immediately after the 9=<placeholder>\x01 field.
    // That is: the first byte of the body (35=MsgType\x01...).
    std::size_t body_start_ = npos;

    // Reserved digits for the BodyLength placeholder in the 9= field.
    // Written as all-zero ASCII digits at first write; backpatched at commit().
    std::size_t bl_digit_count_ = 0;
    // Position of the first placeholder digit (inside the "9=NNN..." field).
    std::size_t bl_digit_pos_ = npos;

    // Scratch memory resource for group bookkeeping (group_writer state).
    std::pmr::memory_resource* scratch_mr_ = nullptr;

    // Track whether we've hit a buffer-full error during appends.
    bool overflow_ = false;
};

// ── group_writer template member ──────────────────────────────────────────────

template <class T>
core::expected_t<void> group_writer::append_field(std::uint16_t tag, T const& v) noexcept {
    if (!owner_) {
        return core::expected_t<void>{};
    }
    return owner_->template append<T>(tag, v);
}

// ── Writer::append template member ────────────────────────────────────────────
// The typed dispatch path: use the 2a/2c trait to_chars to render v into a
// stack-local byte buffer, then delegate to append_raw.
// (C1) the trait call is fenced by core::detail::trap_throw.
//
// T must be a decimal<U>-like type exposing `.format(span)` (2a:
// decimal<U>::format -> traits_type::to_chars, decimal.hpp). NOTE the 2c
// `dict::field_traits<U>` layer is DECODE-only (from_field_view); it has no
// encode/to_chars side, so it is intentionally NOT a target of append<T>.
template <class T>
core::expected_t<void> Writer::append(std::uint16_t tag, T const& v) noexcept {
    // Use a stack-local scratch buffer for the rendered field value.
    // 256 bytes covers all FIX field widths without heap allocation.
    std::array<std::byte, 256> scratch{};
    std::span<std::byte> scratch_span{scratch.data(), scratch.size()};
    // (C1) fence the potentially-throwing trait to_chars call. trap_throw(F)
    // -> expected_t<invoke_result_t<F>>, so a lambda returning
    // expected_t<size_t> yields expected_t<expected_t<size_t>>: the OUTER
    // layer is !has_value only when an exception was trapped; the inner is
    // the format() result.
    auto wrapped = core::detail::trap_throw(
        [&]() noexcept(false) -> core::expected_t<std::size_t> { return v.format(scratch_span); });
    if (!wrapped) {  // exception trapped at the noexcept boundary
        return core::expected_t<void>{std::unexpect, wrapped.error()};
    }
    auto const& fr = *wrapped;  // the format() / trait to_chars result
    if (!fr) {
        return core::expected_t<void>{std::unexpect, fr.error()};
    }
    return append_raw(tag, std::span<const std::byte>{scratch.data(), *fr});
}

}  // namespace fixpp::wire
