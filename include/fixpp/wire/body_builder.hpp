#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/body_builder.hpp
//
// 061-typed-app-messages (061-slim) T005 — wire::body_builder: a body-only FIX
// serializer with a LIFO repeating-group API. Mirrors the C-ABI
// OutboundAccumulator shape (src/capi/message_write.cpp) as a standalone
// C++ primitive for the exemplar builders (Phase 3); internally
// memory_resource-backed (own fixed-buffer arena, not caller-supplied — see
// "ZERO GLOBAL HEAP" below) but the PUBLIC API takes no
// std::pmr::memory_resource parameter.
//
// Anchors: specs/061-typed-app-messages/data-model.md §1 (member table +
//          invariants); contracts/builder-shape-oracle.md C1-C4.
//
// Design:
//   - Ctor records msg_type; "35=<msg_type>\x01" is emitted at commit(), not
//     eagerly (data-model §1).
//   - ZERO GLOBAL HEAP internal accumulation (D/8 no-heap guarantee,
//     [const §VIII.5] self-imposed on outbound builders too — user ruling,
//     061-slim rework): a fixed internal member buffer (`arena_buf_`) backs a
//     `std::pmr::monotonic_buffer_resource` (`arena_`) with a NULL upstream
//     (`std::pmr::null_memory_resource()`), so arena exhaustion THROWS
//     std::bad_alloc rather than silently falling back to global `::operator
//     new` — the fail-closed trigger. Every accumulation container
//     (`entries_`, `open_stack_`, and the nested `entry_node::value_bytes` /
//     `entry_node::instances` / `group_instance::fields`) is a
//     `std::pmr::vector` constructed from `&arena_`. Nested containers are
//     wired via EXPLICIT mr-parameter constructors (mirrors the C-ABI
//     OutboundAccumulator/AccumulatorEntry/GroupInstance model,
//     src/capi/capi_internal.hpp:356-389 + src/capi/message_write.cpp — those
//     call `entries.emplace_back(mr)`/`instances.emplace_back(arena)`/
//     `fields.emplace_back(arena)` explicitly, not uses-allocator
//     construction), so a bug can never silently default to the global-heap
//     resource. Any accumulation op that hits arena exhaustion catches
//     `std::bad_alloc` and returns the typed `wire_frame_too_large` error
//     (INV-4: rolled back to the pre-call container size — no half-mutated
//     entry survives a failed field()/group_begin()/add_entry()/group_begin()
//     call).
//   - group_handle / entry_handle are reallocation-safe VALUE types: each
//     holds an INDEX PATH from the root entry list (never a raw pointer into
//     a body_builder-owned vector element), re-resolved on every access, so
//     the vector growth triggered by add_entry()/group_begin() never dangles
//     a held handle. Mirrors the C-ABI fixpp_group_builder/fixpp_entry
//     parent+index style (src/capi/message_write.cpp:420-434), adapted to a
//     flat fixed-depth index-path array since body_builder owns no arena to
//     heap-allocate builder nodes from.
//   - commit() enforces: INV-2 (no framing tags — rejected eagerly at
//     field()/set_*/group_begin()), INV-3 (canonical decimals — via
//     decimal_t::format at write time), INV-4 (all-or-nothing: any open
//     group / over-cap / undersized `out` -> typed error, `out` untouched),
//     INV-5 (each emitted group instance non-empty + delimiter-first —
//     author-supplied delimiter_tag, no wire->dictionary edge).
//   - Serializes count-precedence (`No<Group>=<N>` before the N instances).
//   - Fixed internal scratch cap of 3800 B (kBodyCap, TU-local in
//     body_builder.cpp, value-equal to the C-ABI kFrameCap at
//     src/capi/message_write.cpp:106, which is file-static and not
//     cross-TU referenceable).
//
// MUST NOT reuse wire::Writer (writer.hpp): Writer always injects the session
// header 8=/9= on first append and 10= at commit(); it has no body-only mode
// (business_messages.cpp precedent, src/session/business_messages.cpp:22-24).
//
// Non-copyable / non-movable: group_handle/entry_handle capture a raw
// `body_builder*` at issue time; relocating (moving) a body_builder would
// silently invalidate every handle already issued. Construct as a scoped
// local and use it in place.

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/decimal_alias.hpp>
#include <fixpp/core/error.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>

namespace fixpp::wire {

class body_builder;
class entry_handle;

// Ceiling on nesting depth for the flat index-path handle representation.
// The 061 exemplar set nests at most 3 levels (E: NoOrders(73) ->
// NoPartyIDs(453) -> NoPartySubIDs(802)); 8 is a generous, purely structural
// bound on the handles' inline storage (no dynamic allocation, no spec
// meaning beyond "large enough").
inline constexpr std::uint8_t kMaxGroupNestDepth = 8;

// group_handle — identifies one open (or already-closed) repeating-group
// entry via an index path from body_builder's root entry list. Trivially
// copyable value type; reallocation-safe (re-resolved by index on every use,
// never a raw pointer into a body_builder-owned vector).
class group_handle {
public:
    group_handle() noexcept = default;

    // Start a new instance of this group. Returns an entry_handle used to
    // populate the instance's fields (data-model §1).
    [[nodiscard]] fixpp::core::expected_t<entry_handle> add_entry() noexcept;

private:
    friend class body_builder;
    friend class entry_handle;

    body_builder* owner_ = nullptr;
    std::array<std::uint32_t, kMaxGroupNestDepth> group_idx_{};
    std::array<std::uint32_t, kMaxGroupNestDepth> inst_idx_{};
    std::uint8_t levels_ = 0;
    std::uint32_t open_seq_ = 0;  // uniquely identifies the group_begin() call
                                  // that produced this handle; used for the
                                  // LIFO top-of-stack check at group_end().
};

// entry_handle — identifies one group-instance (returned by
// group_handle::add_entry()) via the owning group_handle + its instance
// index within that group. Reallocation-safe for the same reason as
// group_handle.
class entry_handle {
public:
    entry_handle() noexcept = default;

    [[nodiscard]] fixpp::core::expected_t<void> set_string(std::uint16_t tag,
                                                           std::string_view v) noexcept;
    [[nodiscard]] fixpp::core::expected_t<void> set_char(std::uint16_t tag, char c) noexcept;
    [[nodiscard]] fixpp::core::expected_t<void> set_int(std::uint16_t tag, std::int64_t v) noexcept;
    [[nodiscard]] fixpp::core::expected_t<void> set_decimal(std::uint16_t tag,
                                                            const fixpp::decimal_t& v) noexcept;

    // Nested group inside this entry (data-model §1 row 21).
    [[nodiscard]] fixpp::core::expected_t<group_handle> group_begin(
        std::uint16_t no_tag, std::uint16_t delimiter_tag) noexcept;

private:
    friend class body_builder;
    friend class group_handle;

    group_handle group_;
    std::uint32_t instance_index_ = 0;
};

// body_builder — body-only FIX serializer (data-model.md §1).
class body_builder {
public:
    explicit body_builder(std::string_view msg_type) noexcept;

    body_builder(const body_builder&) = delete;
    body_builder& operator=(const body_builder&) = delete;
    body_builder(body_builder&&) = delete;
    body_builder& operator=(body_builder&&) = delete;

    // Flat fields (data-model §1). Author order is preserved verbatim in the
    // emitted body (contract C1/C3).
    [[nodiscard]] fixpp::core::expected_t<void> field(std::uint16_t tag,
                                                      std::string_view v) noexcept;
    [[nodiscard]] fixpp::core::expected_t<void> field(std::uint16_t tag, char c) noexcept;
    [[nodiscard]] fixpp::core::expected_t<void> field(std::uint16_t tag, std::int64_t v) noexcept;
    [[nodiscard]] fixpp::core::expected_t<void> field(std::uint16_t tag,
                                                      const fixpp::decimal_t& v) noexcept;

    // Open a top-level repeating group. `delimiter_tag` is the group's
    // first-field tag; the AUTHOR supplies it (no dictionary lookup) and
    // commit() enforces it (INV-5).
    [[nodiscard]] fixpp::core::expected_t<group_handle> group_begin(
        std::uint16_t no_tag, std::uint16_t delimiter_tag) noexcept;

    // LIFO close: `handle` must be the innermost still-open group.
    [[nodiscard]] fixpp::core::expected_t<void> group_end(group_handle handle) noexcept;

    // Validate + serialize into `out`, atomically (INV-4): `out` is untouched
    // on any failure path. Returns the written body subspan on success.
    [[nodiscard]] fixpp::core::expected_t<std::span<std::byte>> commit(
        std::span<std::byte> out [[clang::lifetimebound]]) noexcept;

private:
    friend class group_handle;
    friend class entry_handle;

    // ── Recursive accumulator node types ──────────────────────────────────
    //
    // Mutual recursion (an entry_node can hold a group of group_instance,
    // each holding a list of entry_node) is broken exactly as
    // capi_internal.hpp/message_write.cpp break it for
    // AccumulatorEntry/GroupInstance: group_instance is declared FIRST with
    // its `fields` vector<entry_node> over an as-yet-incomplete entry_node
    // (legal since C++17: std::vector<T>'s layout does not depend on T's
    // completeness); group_instance's special members are DECLARED here and
    // DEFINED in body_builder.cpp once entry_node is complete. entry_node is
    // declared SECOND: its ctor takes the arena resource explicitly (needed
    // to construct its own pmr vectors from the right resource) but its
    // move/dtor can still use `= default` because group_instance is already
    // a complete type by that point.
    //
    // ZERO-HEAP: both node types are std::pmr containers, constructed with an
    // EXPLICIT `std::pmr::memory_resource*` (mirrors AccumulatorEntry(mr) /
    // GroupInstance(mr), src/capi/capi_internal.hpp:356-389) so every nested
    // vector is unambiguously wired to body_builder::arena_ — never the
    // default global-heap-backed pmr resource.
    struct entry_node;

    struct group_instance {
        std::pmr::vector<entry_node> fields;

        explicit group_instance(std::pmr::memory_resource* mr);
        ~group_instance();
        group_instance(group_instance&&) noexcept;
        group_instance& operator=(group_instance&&) noexcept;
        group_instance(const group_instance&) = delete;
        group_instance& operator=(const group_instance&) = delete;
    };

    struct entry_node {
        std::uint16_t tag = 0;
        bool is_group = false;
        std::uint16_t delimiter_tag = 0;             // meaningful only if is_group
        std::pmr::vector<std::byte> value_bytes;     // scalar payload
        std::pmr::vector<group_instance> instances;  // group payload

        explicit entry_node(std::pmr::memory_resource* mr) : value_bytes(mr), instances(mr) {}
        ~entry_node() = default;
        entry_node(entry_node&&) = default;
        entry_node& operator=(entry_node&&) = default;
        entry_node(const entry_node&) = delete;
        entry_node& operator=(const entry_node&) = delete;
    };

    // Re-resolve a group_handle / entry_handle to its live node, walking the
    // index path fresh from entries_ every call (reallocation-safe).
    entry_node* resolve_group(const group_handle& h) noexcept;
    group_instance* resolve_instance(const entry_handle& e) noexcept;
    [[nodiscard]] bool is_innermost_open(std::uint32_t open_seq) const noexcept;

    // Shared validated-append helpers (INV-2 framing-tag reject + value
    // validation), used by both the top-level field()s and entry_handle's
    // set_*s via the `into` vector (either entries_ or a resolved instance's
    // fields). Lifted from src/session/business_messages.cpp's anonymous-
    // namespace wfield/wchar/wdecimal + is_clean_field_value (SOH-injection
    // guard, [RC#1: gate-b/r1]).
    // Core append: emplace one scalar entry and assign its pre-encoded value
    // bytes, rolling back the node on arena exhaustion (std::bad_alloc ->
    // wire_frame_too_large). Callers pre-validate (framing-tag reject + value
    // validation); this helper does NOT re-validate.
    static fixpp::core::expected_t<void> append_bytes_field(
        std::pmr::vector<entry_node>& into, std::uint16_t tag,
        std::span<const std::byte> value) noexcept;
    static fixpp::core::expected_t<void> append_string_field(std::pmr::vector<entry_node>& into,
                                                             std::uint16_t tag,
                                                             std::string_view v) noexcept;
    static fixpp::core::expected_t<void> append_int_field(std::pmr::vector<entry_node>& into,
                                                          std::uint16_t tag,
                                                          std::int64_t v) noexcept;
    static fixpp::core::expected_t<void> append_decimal_field(std::pmr::vector<entry_node>& into,
                                                              std::uint16_t tag,
                                                              const fixpp::decimal_t& v) noexcept;

    fixpp::core::expected_t<entry_handle> add_entry_impl(const group_handle& g) noexcept;
    fixpp::core::expected_t<group_handle> entry_group_begin_impl(
        const entry_handle& e, std::uint16_t no_tag, std::uint16_t delimiter_tag) noexcept;

    // INV-5: every group instance (recursive) is non-empty and delimiter-
    // first.
    static fixpp::core::expected_t<void> validate_group_grammar(
        const std::pmr::vector<entry_node>& entries) noexcept;

    static std::size_t compute_size(const std::pmr::vector<entry_node>& entries) noexcept;
    static void serialize_entries(std::byte* buf, std::size_t& pos,
                                  const std::pmr::vector<entry_node>& entries) noexcept;

    std::string msg_type_;

    // ── Zero-global-heap arena (061-slim rework) ────────────────────────────
    // Fixed internal scratch for the intermediate accumulation TREE (entry
    // nodes + value bytes + group instances). Separate from kBodyCap (3800 B,
    // body_builder.cpp) which caps the SERIALIZED body -- this cap is on the
    // (larger, node-overhead-inflated) accumulator tree. 16384 B is generous
    // headroom for the 5 061-slim exemplar shapes (≤3-level nesting, small
    // field counts) plus the OverCap test's single 4000 B field. Null
    // upstream: exhaustion THROWS std::bad_alloc (never silently falls back
    // to global ::operator new) -- the fail-closed trigger caught by every
    // arena-touching accumulation op below.
    static constexpr std::size_t kArenaCap = 16384;
    // Not value-initialized: a monotonic_buffer_resource only reads bytes its
    // pmr containers have first written, so zeroing this 16 KB backing on every
    // ctor is pure waste.
    std::array<std::byte, kArenaCap> arena_buf_;
    std::pmr::monotonic_buffer_resource arena_{arena_buf_.data(), arena_buf_.size(),
                                               std::pmr::null_memory_resource()};

    std::pmr::vector<entry_node> entries_{&arena_};
    // LIFO stack of open groups. Only each open group's `open_seq_` is read
    // (group_end top-of-stack check); storing the full group_handle would waste
    // ~68 B of arena per open group for state never re-resolved from the stack.
    std::pmr::vector<std::uint32_t> open_stack_{&arena_};
    std::uint32_t next_open_seq_ = 1;
};

}  // namespace fixpp::wire
