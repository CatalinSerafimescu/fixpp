#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/view.hpp
// [2b §4.1] View flyweight base + detail::generation_token.
// Authority: .specify/2b-wire.md v0.2. Shape oracle: specs/004-wire-codec/
// contracts/view.hpp. Every wire view type (MessageView, frame_view,
// field_view, group_view, unknown_fields_view) is a non-owning flyweight
// over the originating frame buffer ([2b §612]).
//
// Release: the debug generation token is stripped via [[no_unique_address]]
// + #ifndef NDEBUG so a View is exactly {data ptr, length} and stays
// trivially copyable. Debug: View::check_alive() traps (does NOT throw —
// it sits inside the noexcept parse->fromApp window) on use-after-
// buffer-reuse, detected via a per-pool generation counter.

#include <cstddef>
#include <cstdint>
#include <span>

#ifndef NDEBUG
#include <array>
#include <cstdlib>
#endif

namespace fixpp::wire {

namespace detail {

// 4-byte POD: (pool_id, gen). A per-message/per-session arena owns a pool
// slot; resetting the arena bumps that slot's live generation. A captured
// token whose gen no longer matches the live generation for its pool_id is
// a use-after-buffer-reuse and traps in debug. Release: zero-sized.
struct generation_token {
    std::uint16_t pool_id = 0;
    std::uint16_t gen = 0;
};

#ifndef NDEBUG
// Bounded thread-local registry of live pool generations. Single-thread-
// per-session (`[const §XI.4]`), so a plain thread_local array is correct
// and lock-free. pool_id 0 is the "untracked" sentinel (never traps).
inline constexpr std::size_t max_tracked_pools = 256;

inline std::uint16_t* pool_generation_slot(std::uint16_t pool_id) noexcept {
    static thread_local std::array<std::uint16_t, max_tracked_pools> live{};
    if (pool_id == 0 || pool_id >= max_tracked_pools) {
        return nullptr;  // untracked / out-of-range: never traps
    }
    return &live[pool_id];
}

// Called by an arena when it reuses its backing storage: any View still
// holding the previous generation will trap on its next check_alive().
inline generation_token bump_pool_generation(std::uint16_t pool_id) noexcept {
    std::uint16_t* slot = pool_generation_slot(pool_id);
    std::uint16_t g = 0;
    if (slot != nullptr) {
        ++(*slot);
        g = *slot;
    }
    return generation_token{.pool_id = pool_id, .gen = g};
}

inline generation_token current_pool_token(std::uint16_t pool_id) noexcept {
    std::uint16_t* slot = pool_generation_slot(pool_id);
    return generation_token{.pool_id = pool_id,
                            .gen = (slot != nullptr) ? *slot : std::uint16_t{0}};
}
#endif

}  // namespace detail

// One repeating-group instance: a (ptr,len) sub-frame slice into the
// originating frame buffer. Materialized once into the per-message
// OffsetTable arena ([2b §4.4] "all storage from the captured mr");
// group_view only borrows it (no thread-local, no per-call rebuild).
struct group_slice {
    std::byte const* data = nullptr;
    std::size_t len = 0;
};

class View {
public:
    constexpr View() noexcept = default;

    [[nodiscard]] std::span<const std::byte> bytes() const noexcept
        [[clang::lifetimebound]] {
        check_alive();  // [2b §6.4] trap on use-after-buffer-reuse in debug
        return {data_, len_};
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return len_ == 0; }

protected:
    constexpr View(std::byte const* data [[clang::lifetimebound]], std::size_t len,
                   detail::generation_token gen) noexcept
        : data_{data},
          len_{len}
#ifndef NDEBUG
          ,
          gen_{gen}
#endif
    {
#ifdef NDEBUG
        (void)gen;
#endif
    }

#ifndef NDEBUG
    // Traps (terminate-equivalent) — never throws — on use after the
    // originating buffer's pool was reset/reused. Inside the noexcept
    // parse->fromApp window per [2b §6.4] / FR-016.
    void check_alive() const noexcept {
        std::uint16_t const* slot = detail::pool_generation_slot(gen_.pool_id);
        if (slot != nullptr && *slot != gen_.gen) {
            std::abort();  // use-after-buffer-reuse — debug trap
        }
    }
#else
    constexpr void check_alive() const noexcept {}
#endif

    [[nodiscard]] constexpr std::byte const* data_ptr() const noexcept { return data_; }
    [[nodiscard]] constexpr std::size_t data_len() const noexcept { return len_; }
#ifndef NDEBUG
    [[nodiscard]] constexpr detail::generation_token token() const noexcept { return gen_; }
#else
    [[nodiscard]] constexpr detail::generation_token token() const noexcept { return {}; }
#endif

private:
    std::byte const* data_{};
    std::size_t len_{};
#ifndef NDEBUG
    detail::generation_token gen_{};
#endif
};

}  // namespace fixpp::wire
