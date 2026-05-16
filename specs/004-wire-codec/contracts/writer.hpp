// SPDX-License-Identifier: AGPL-3.0-or-later
// CONTRACT SHAPE ORACLE — extract of [2b §4.5]. Authority: 2b-wire.md v0.2.
// commit(): digit-only BodyLength (memmove backpatch) + byte-sum-mod-256
// CheckSum (3 zero-padded ASCII digits). NO space padding ([FIX50SP2 §3.3]).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <fixpp/core/error.hpp>

namespace fixpp::wire {

class group_writer {
public:
    [[nodiscard]] core::expected_t<void>
    append_field(std::uint16_t tag, std::span<const std::byte> value) noexcept;
    template <class T>
    [[nodiscard]] core::expected_t<void>
    append_field(std::uint16_t tag, T const& v) noexcept;
    void close() && noexcept;   // RAII fallback seals NoXxx count
};

class Writer {
public:
    explicit Writer(std::span<std::byte> dst [[clang::lifetimebound]],
                    std::pmr::memory_resource* scratch_mr
                        [[clang::lifetimebound]]) noexcept;

    [[nodiscard]] core::expected_t<void>
    append_raw(std::uint16_t tag, std::span<const std::byte> value) noexcept;
    template <class T>
    [[nodiscard]] core::expected_t<void> append(std::uint16_t tag, T const& v) noexcept;

    [[nodiscard]] core::expected_t<group_writer>
    open_group(std::uint16_t no_tag, std::uint32_t count) noexcept
        [[clang::lifetimebound]];   // LIFO nesting

    [[nodiscard]] core::expected_t<std::size_t> commit() && noexcept;
    [[nodiscard]] std::size_t bytes_written() const noexcept;
};

}  // namespace fixpp::wire
