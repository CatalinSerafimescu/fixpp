#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/group_view.hpp
// [2b §4.7] group_view<GroupT> : View. iter() and operator[] MUST enumerate
// identical entries/order incl. nested groups (seam #8). GroupT is generated
// by 2c (e.g. NewOrderSingle::leg) and is constructible from a sub-frame
// (byte span + generation token); the wire layer never decodes fields.
// Authority: .specify/2b-wire.md v0.2; shape oracle contracts/group_view.hpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>

// 062 T002: entry_context needs OffsetTable::group_member_fn_t (a complete
// nested typedef) + OffsetTable* (pointer-only). offset_table.hpp only
// includes framer.hpp/view.hpp (no back-edge to this header), so this
// include is a plain one-directional edge — not a cycle.
#include "offset_table.hpp"
#include "view.hpp"

namespace fixpp::wire {

// [2b §4.7] 063 T004: the context-scoped membership key — `(msg_type,
// bounded parent-no_tag-path)` — Gate A Round-1/-2 Option A (data-model.md
// §"GroupMembership", plan.md Complexity Tracking). `no_tag` itself is NOT
// part of this struct: it is the separate argument every group_member_fn_t
// call already carries, so the key a predicate answers is
// `(group_context, no_tag) -> members`. Defined HERE (not offset_table.hpp)
// per 063 data-model.md/tasks.md T004; offset_table.hpp only forward-declares
// this type (a plain one-directional edge, group_view.hpp -> offset_table.hpp,
// stays intact — OffsetTable stores the constituent fields raw, never a
// `group_context` by value, to avoid a header cycle back into this file).
//
// Trivially copyable, fixed-size (K=16 mirrors codegen's kMaxGroupDepth,
// emit_messages.cpp:137) — no allocation, by-value ride-along in
// entry_context (FR-004).
struct group_context {
    std::string_view msg_type{};  // aliases the MESSAGE WIRE BUFFER (data-model.md:28) — NOT
                                   // dictionary scratch; outlives every nested entry (one-parse,
                                   // ROOT-owned lifetime, same as entry_context::span).
    std::array<std::uint16_t, 16> parent_path{};  // bounded parent-no_tag chain
    std::uint8_t depth = 0;                       // valid prefix length of parent_path

    // Returns a NEW context with `no_tag` appended to the parent path — used
    // when minting the entry_context for a group's own occurrences (the
    // entry's "own" context = its container path + its own no_tag), so a
    // later nested descent from that entry seeds the correct sub-table
    // context (plan.md Context-propagation mechanism). Silently clamps at
    // K=16 (depth stays put, no_tag dropped) rather than overflow the fixed
    // array; the fail-closed K=16 overflow disposition (err_group_too_large)
    // is a US2 (063 T022) concern, not this plumbing seam.
    [[nodiscard]] constexpr group_context pushed(std::uint16_t no_tag) const noexcept {
        group_context out = *this;
        if (out.depth < parent_path.size()) {
            out.parent_path[out.depth] = no_tag;
            ++out.depth;
        }
        return out;
    }
};

static_assert(std::is_trivially_copyable_v<group_context>,
              "group_context must be trivially copyable (FR-004 zero-alloc-by-value context)");

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
    // 063 T005: the context-scoped membership key (msg_type + bounded parent
    // path) this entry's OWN group occurs under. Live (US1): set when the
    // entry is minted (parser.hpp seeds it as the container path PUSHED with
    // the group's no_tag) and threaded into nested_group_slices() on a nested
    // descent, which seeds the sub-table's stored context — so the membership
    // predicate resolves via this exact context (Defect A fixed), no longer
    // the bare-no_tag global. See group_context above.
    group_context group_context{};
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
