#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/unknown_fields.hpp
// [2b §4.8] unknown_fields_view : View. Yields ONLY dictionary-MISSING tags
// (dictionary-known-but-invalid-for-MsgType tags are NOT here — those raise
// wire_unexpected_tag via validator rule 5). No vector materialization: the
// iterator walks a borrowed span of (tag,value-slice) pairs the parser
// recorded in document order, so round-trip preserves original byte order.
// Authority: .specify/2b-wire.md v0.2; shape oracle contracts/unknown_fields.hpp.

#include <cstddef>
#include <cstdint>
#include <span>

#include "view.hpp"

namespace fixpp::wire {

class unknown_fields_view : public View {
public:
    struct kv {
        std::uint16_t tag = 0;
        std::byte const* data = nullptr;
        std::size_t len = 0;
    };

    constexpr unknown_fields_view() noexcept = default;
    unknown_fields_view(std::span<kv const> items, detail::generation_token gen) noexcept
        : View{items.empty() ? nullptr : items.front().data, items.empty() ? 0 : items.front().len,
               gen},
          items_{items} {}

    class iterator {
    public:
        explicit iterator(kv const* p) noexcept : p_{p} {}
        [[nodiscard]] kv const& operator*() const noexcept { return *p_; }
        iterator& operator++() noexcept {
            ++p_;
            return *this;
        }
        [[nodiscard]] bool operator==(iterator const& o) const noexcept { return p_ == o.p_; }

    private:
        kv const* p_;
    };

    [[nodiscard]] iterator begin() const noexcept [[clang::lifetimebound]] {
        return iterator{items_.data()};
    }
    [[nodiscard]] iterator end() const noexcept [[clang::lifetimebound]] {
        return iterator{items_.data() + items_.size()};
    }
    // Contract-mandated API (shape oracle contracts/unknown_fields.hpp):
    // "no unknown fields", deliberately distinct from View::empty()'s
    // "flyweight has no bytes". The shadow is intentional, not a slicing
    // hazard (value type, no virtual dispatch). [see 004-wire-codec-verify §run 2]
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }

private:
    std::span<kv const> items_;
};

}  // namespace fixpp::wire
