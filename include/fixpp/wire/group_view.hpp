#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/group_view.hpp
// [2b §4.7] group_view<GroupT> : View. iter() and operator[] MUST enumerate
// identical entries/order incl. nested groups (seam #8). GroupT is generated
// by 2c (e.g. NewOrderSingle::leg) and is constructible from a sub-frame
// (byte span + generation token); the wire layer never decodes fields.
// Authority: .specify/2b-wire.md v0.2; shape oracle contracts/group_view.hpp.

#include <cstddef>
#include <memory_resource>
#include <span>
#include <type_traits>

// 062 T002: entry_context needs OffsetTable::group_member_fn_t (a complete
// nested typedef) + OffsetTable* (pointer-only). offset_table.hpp only
// includes framer.hpp/view.hpp (no back-edge to this header), so this
// include is a plain one-directional edge — not a cycle.
#include "offset_table.hpp"
#include "view.hpp"

namespace fixpp::wire {

// [2b §4.7] entry_context (062 T002): a by-value repeating-group entry
// (generated `G_<no_tag>`) built from `{span}` alone structurally cannot
// carry out its own read contract — no handle to the parent cache, no
// stable occurrence identity, no token to mint views. entry_context threads
// everything the entry needs, all borrowed/copied from the parent
// OffsetTable/MessageView: trivially copyable, no allocation (data-model.md
// §"entry read context `entry_context`", RC2/N1/N2).
struct entry_context {
    std::span<const std::byte> span;          // this entry's own slice bytes
    std::pmr::memory_resource* mr = nullptr;  // parent per-message PMR arena
    void const* opaque_dict = nullptr;        // dictionary handle (nested slicer)
    OffsetTable::group_member_fn_t group_member_fn =
        nullptr;                     // dict-driven group-membership predicate
    detail::generation_token gen{};  // [2b §6.4] REQUIRED (N1) — never a default {} token
    OffsetTable const* parent_cache_owner =
        nullptr;  // root OffsetTable owning the single flat nested-view cache (RC2); const — only
                  // ever calls the const nested_group_slices()
    std::byte const* outer_occurrence_id =
        nullptr;  // this entry slice's globally-unique data-pointer identity
};

// FR-004 zero-alloc-by-value entry: entry_context (and therefore every
// generated G_<no_tag> that stores one) must stay trivially copyable.
static_assert(std::is_trivially_copyable_v<entry_context>,
              "entry_context must be trivially copyable (FR-004 zero-alloc-by-value entry)");

template <class GroupT>
class group_view : public View {
public:
    constexpr group_view() noexcept = default;

    // instances: a span of (byte-offset,length) sub-frame slices, one per
    // repeating-group occurrence, in document order. Owned by the
    // OffsetTable's per-message arena; group_view only borrows it.
    using slice = group_slice;

    // Back-compat seam (062 T007): threads `gen` into a base entry_context so
    // any future dict-free caller of this ctor still gets a real generation
    // token on every minted entry (mr/opaque_dict/group_member_fn/
    // parent_cache_owner stay null — no production caller uses this path;
    // MessageView::group<>() uses the entry_context ctor below).
    group_view(std::span<slice const> instances, detail::generation_token gen) noexcept
        : View{instances.empty() ? nullptr : instances.front().data,
               instances.empty() ? 0 : instances.front().len, gen},
          instances_{instances},
          base_ctx_{.gen = gen} {}

    // [2b §4.7] 062 T007: `base` carries everything a generated entry needs to
    // read its own fields/nested groups (mr, opaque_dict, group_member_fn,
    // generation token, parent_cache_owner = the root OffsetTable). Per-entry
    // span/outer_occurrence_id are irrelevant on `base` — operator[]/iterator
    // override both from the SAME instance slice below.
    group_view(std::span<slice const> instances, entry_context base) noexcept
        : View{instances.empty() ? nullptr : instances.front().data,
               instances.empty() ? 0 : instances.front().len, base.gen},
          instances_{instances},
          base_ctx_{base} {}

    [[nodiscard]] std::size_t size() const noexcept {
        check_alive();
        return instances_.size();
    }

    // Both operator[](i) and iterator::operator*() (which delegates to this)
    // derive the per-entry entry_context from the SAME instance slice `i` —
    // identical span + identical outer_occurrence_id (seam #8 / INV-G4).
    [[nodiscard]] GroupT operator[](std::size_t i) const noexcept [[clang::lifetimebound]] {
        check_alive();
        auto const& s = instances_[i];
        entry_context ctx = base_ctx_;
        ctx.span = std::span<const std::byte>{s.data, s.len};
        ctx.outer_occurrence_id = s.data;
        return GroupT{ctx};
    }

    class iterator {
    public:
        iterator(group_view const* gv, std::size_t i) noexcept : gv_{gv}, i_{i} {}
        [[nodiscard]] GroupT operator*() const noexcept { return (*gv_)[i_]; }
        iterator& operator++() noexcept {
            ++i_;
            return *this;
        }
        [[nodiscard]] bool operator==(iterator const& o) const noexcept { return i_ == o.i_; }

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
        check_alive();
        return iterator{this, instances_.size()};
    }

private:
    std::span<slice const> instances_;
    entry_context base_ctx_{};
};

}  // namespace fixpp::wire
