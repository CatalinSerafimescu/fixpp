#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/group_view.hpp
// [2b §4.7] group_view<GroupT> : View. iter() and operator[] MUST enumerate
// identical entries/order incl. nested groups (seam #8). GroupT is generated
// by 2c (e.g. NewOrderSingle::leg) and is constructible from a sub-frame
// (byte span + generation token); the wire layer never decodes fields.
// Authority: .specify/2b-wire.md v0.2; shape oracle contracts/group_view.hpp.

#include <cstddef>
#include <span>

#include "view.hpp"

namespace fixpp::wire {

template <class GroupT>
class group_view : public View {
public:
    constexpr group_view() noexcept = default;

    // instances: a span of (byte-offset,length) sub-frame slices, one per
    // repeating-group occurrence, in document order. Owned by the parser's
    // per-message arena; group_view only borrows it.
    struct slice {
        std::byte const* data = nullptr;
        std::size_t len = 0;
    };

    group_view(std::span<slice const> instances,
               detail::generation_token gen) noexcept
        : View{instances.empty() ? nullptr : instances.front().data,
               instances.empty() ? 0 : instances.front().len, gen},
          instances_{instances} {}

    [[nodiscard]] std::size_t size() const noexcept {
        return instances_.size();
    }

    [[nodiscard]] GroupT
    operator[](std::size_t i) const noexcept [[clang::lifetimebound]] {
        auto const& s = instances_[i];
        return GroupT{std::span<const std::byte>{s.data, s.len}};
    }

    class iterator {
    public:
        iterator(group_view const* gv, std::size_t i) noexcept
            : gv_{gv}, i_{i} {}
        [[nodiscard]] GroupT operator*() const noexcept { return (*gv_)[i_]; }
        iterator& operator++() noexcept {
            ++i_;
            return *this;
        }
        [[nodiscard]] bool operator==(iterator const& o) const noexcept {
            return i_ == o.i_;
        }

    private:
        group_view const* gv_;
        std::size_t i_;
    };

    // .iter() skips any sub-index build — walks the same instance slices
    // operator[] addresses, so the two MUST agree (seam #8).
    [[nodiscard]] iterator iter() const noexcept [[clang::lifetimebound]] {
        return iterator{this, 0};
    }
    [[nodiscard]] iterator begin() const noexcept [[clang::lifetimebound]] {
        return iterator{this, 0};
    }
    [[nodiscard]] iterator end() const noexcept [[clang::lifetimebound]] {
        return iterator{this, instances_.size()};
    }

private:
    std::span<slice const> instances_{};
};

}  // namespace fixpp::wire
