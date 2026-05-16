#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/framer.hpp
// [2b §4.2] Framer / pmr_carry_buffer / frame_view SURFACE declarations.
// Authority: .specify/2b-wire.md v0.2. Shape oracle: specs/004-wire-codec/
// contracts/framer.hpp. Relocated to Foundational per /analyze finding I1
// (depends only on View; parser.hpp #includes this). The framing ALGORITHM
// (Framer::feed body, carry handling, mandatory CheckSum/BodyLength verify)
// is src/wire/framer.cpp — US3 / T040.

#include <cstddef>
#include <fixpp/core/error.hpp>  // core::expected_t
#include <memory_resource>
#include <span>
#include <vector>

#include "view.hpp"

namespace fixpp::wire {

inline constexpr std::size_t default_max_frame_bytes =
    std::size_t{256} * std::size_t{1024};  // 256 KiB

// Backed by SessionConfig::framer_carry_arena (SESSION lifetime, NOT
// per-message — carry spans messages). One allocation at construction from
// the supplied memory_resource; never reallocates. Append past capacity is
// reported by the Framer as wire_frame_too_large ([2b §6.1.3]).
class pmr_carry_buffer {
public:
    pmr_carry_buffer(std::size_t capacity, std::pmr::memory_resource* mr) noexcept
        : buf_(mr), cap_(capacity) {
        buf_.reserve(capacity);  // the single session-lifetime allocation
    }

    [[nodiscard]] std::size_t size() const noexcept { return buf_.size(); }
    [[nodiscard]] std::size_t capacity() const noexcept { return cap_; }
    [[nodiscard]] bool empty() const noexcept { return buf_.empty(); }

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept [[clang::lifetimebound]] {
        return {buf_.data(), buf_.size()};
    }

    // Returns false when the append would exceed the fixed capacity (the
    // Framer maps that to wire_frame_too_large); never reallocates.
    [[nodiscard]] bool append(std::span<const std::byte> in) noexcept {
        if (buf_.size() + in.size() > cap_) {
            return false;
        }
        buf_.insert(buf_.end(), in.begin(), in.end());
        return true;
    }

    // Drop the first n consumed bytes (a complete frame was emitted),
    // keeping the trailing partial carry. Bounded one-frame move.
    void consume_front(std::size_t n) noexcept {
        if (n >= buf_.size()) {
            buf_.clear();
            return;
        }
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(n));
    }

    void clear() noexcept { buf_.clear(); }

private:
    std::pmr::vector<std::byte> buf_;
    std::size_t cap_ = 0;
};

// One complete, framing-verified FIX message ([2b §4.2]). View base spans
// the whole frame (8=...SOH..10=NNN SOH); body() spans the BodyLength-
// covered range (the byte after 9=BodyLength's SOH .. the byte before
// 10=CheckSum's first digit). v0.1's checksum()/body_length() were
// intentionally removed (Codex P3) — the Framer verifies them before any
// frame_view is exposed; consumers never re-derive them.
class frame_view : public View {
public:
    constexpr frame_view() noexcept = default;

    [[nodiscard]] constexpr std::span<const std::byte> body() const noexcept
        [[clang::lifetimebound]] {
        return {data_ptr() + body_off_, body_len_};
    }

protected:
    // The (frame_len, body_off, body_len) run is the [2b §4.2] frame_view
    // shape oracle — fixed signature, minted only by the parser/factory.
    // Reshaping into a params struct would break the contract extract, so
    // the swappable-parameters lint is suppressed here rather than papered.
    // NOLINTBEGIN(bugprone-easily-swappable-parameters)
    constexpr frame_view(std::byte const* frame [[clang::lifetimebound]], std::size_t frame_len,
                         std::size_t body_off, std::size_t body_len,
                         detail::generation_token gen) noexcept
        // NOLINTEND(bugprone-easily-swappable-parameters)
        : View{frame, frame_len, gen}, body_off_{body_off}, body_len_{body_len} {}

    // The Framer algorithm (src/wire/framer.cpp, T040) and the test-only
    // frame_view_factory (T008) are the only producers of a populated
    // frame_view; both mint it through this protected ctor.
    friend class Framer;
    friend struct frame_view_access;

private:
    std::size_t body_off_ = 0;
    std::size_t body_len_ = 0;
};

class Framer {
public:
    struct Config {
        std::size_t max_frame_bytes = default_max_frame_bytes;
        // CheckSum + BodyLength verification is MANDATORY and not
        // configurable ([2b §2]); no production bypass exists.
    };

    // The shape oracle writes `explicit Framer(Config c = {})`; a `= {}`
    // default arg on a nested struct with default member initializers is a
    // DMI-before-complete-class error, so the real header splits it into
    // two ctors — the callable surface (Framer{} and Framer{cfg}) is
    // identical.
    Framer() noexcept = default;
    explicit Framer(Config c) noexcept : cfg_{c} {}

    // Consumes an arbitrary byte chunk, emits zero or more complete,
    // framing-verified frames into `out`, carries the trailing partial
    // into `carry`. Implemented in src/wire/framer.cpp (T040 — US3).
    [[nodiscard]] core::expected_t<std::span<frame_view>> feed(
        std::span<const std::byte> incoming [[clang::lifetimebound]],
        pmr_carry_buffer& carry [[clang::lifetimebound]],
        std::span<frame_view> out [[clang::lifetimebound]]) noexcept [[clang::lifetimebound]];

    [[nodiscard]] std::size_t pending_bytes() const noexcept { return pending_; }

private:
    Config cfg_{};
    std::size_t pending_ = 0;
};

}  // namespace fixpp::wire
