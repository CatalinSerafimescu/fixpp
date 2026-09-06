#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/offset_table.hpp
// [2b §4.4] OffsetTable + entry. Authority: .specify/2b-wire.md v0.2; shape
// oracle contracts/offset_table.hpp. Eager entry[] in document order +
// open-address robin-hood overlay for O(1)-by-tag first-occurrence find;
// lazy group sub-index on first group(no_tag). All storage from the captured
// per-message memory_resource. DoS caps: wire_offset_table_full (>4096 occ),
// wire_tag_out_of_range, wire_group_too_large — bounded memory, no
// unbounded growth ([2b §1.2]).

#include <array>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include "framer.hpp"
#include "view.hpp"  // group_slice (mr-backed group instance slices)

namespace fixpp::wire {

inline constexpr std::size_t default_max_offset_entries = 4096;  // occ space
inline constexpr std::size_t default_max_group_entries_per_instance = 4096;

// 063 T004/T006: the context-scoped membership key (msg_type + bounded
// parent-no_tag path) — full definition lives in group_view.hpp (data-model.md
// §"GroupMembership"/§"entry_context"). Forward-declared here: the typedef and
// method signatures below only need `group_context const&` (reference
// parameter — complete type not required for a declaration), and completing it
// here would require including group_view.hpp, which itself includes THIS
// header — a cycle the file's one-directional include edge (see
// group_view.hpp:15-18) must not acquire. OffsetTable stores the constituent
// fields raw (never a `group_context` member by value) for the same reason;
// the .cpp includes group_view.hpp for the complete type where it is
// actually constructed/consumed.
struct group_context;

// 073 T002: status-bearing return of both `nested_group_slices` overloads
// (data-model.md §"nested_slices_result"; contracts/nested_slices_result.md).
// `alloc_failed == true` iff a sub-view allocation failed via any of THREE
// origins (research.md §D2, extended per
// feedback_status_origin_must_cover_all_alloc_catch_sites): (a) the
// sub-OffsetTable shell allocation failed (`build_nested_subview` -> nullptr),
// (b) the sub-table built non-null but its own `group_slices_status()` caught
// `bad_alloc` during slice materialization, or (c) the sub-table built
// non-null but its OWN internal `build()` degraded on bad_alloc
// (`status_ = out_of_memory`, offset_table.cpp:366-370) — a noexcept ctor
// degrade that never returns nullptr, so `nested_group_slices` must check
// `table->build_status()` explicitly at both resolution exits (mode (c) is
// otherwise silently indistinguishable from a legitimately absent/count-0
// group, since a degraded table's `group()` call returns without throwing).
// Trivially copyable (span + bool) — zero-alloc wire-read discipline
// preserved.
struct nested_slices_result {
    std::span<group_slice const> slices{};
    bool alloc_failed = false;
};
static_assert(std::is_trivially_copyable_v<nested_slices_result>);

// 073 T002: INTERNAL status-bearing form of `OffsetTable::group_slices`
// (research.md §D2 mode (b), FR-002 originate-at-failure). Only
// `nested_group_slices` consumes this — NOT a public return type. The public
// `group_slices(no_tag)` stays a span-returning one-line wrapper so every
// top-level caller (C-ABI top-level group getter, `MessageView::group<>()`)
// is unaffected (L-073-1, deferred).
struct group_slices_result {
    std::span<group_slice const> slices{};
    bool alloc_failed = false;
};
static_assert(std::is_trivially_copyable_v<group_slices_result>);

class OffsetTable {
public:
    using group_member_fn_t = bool (*)(void const*, group_context const&, std::uint16_t,
                                       std::uint16_t) noexcept;

    // 083 T057 (C-8.1): SIBLING of `group_member_fn_t`, resolving through the
    // SAME `opaque_dict` to `table_view::group_first_field(msg_type,
    // parent_path, no_tag)`. The callback set was membership-only; this widens
    // it by exactly one entry. Internal seam — no public signature changes
    // (C-8.3): `group_slices()` / `group_slices_status()` keep their
    // signatures. Returns 0 when `no_tag` is not a group in that context.
    using group_delim_fn_t = std::uint16_t (*)(void const*, group_context const&,
                                               std::uint16_t) noexcept;

    // Caller-tunable DoS caps (FR-015 / [2b §1.2] "configurable").
    // Defaults match the module-level inline constexpr above.
    struct Config {
        std::size_t max_offset_entries = default_max_offset_entries;
        std::size_t max_group_entries_per_instance = default_max_group_entries_per_instance;
    };

    struct entry {
        // Members ordered offset/length first so the struct packs to exactly
        // 12 bytes with alignof 4 (the [2b §4.4] invariant). The shape-oracle
        // extract lists them tag-first; member order in an extract is
        // non-binding — the named set + sizeof==12/alignof==4 are.
        std::uint32_t offset;            // value offset into the frame
        std::uint32_t length;            // value length (after '=', pre-SOH)
        std::uint16_t tag;               // 0..65535 ([2b §1.2])
        std::uint16_t group_index_link;  // 0 = top-level
    };
    static_assert(sizeof(entry) == 12);
    static_assert(alignof(entry) == 4);

    // A default-constructed table is empty and reports every field absent
    // (status_ = ok, no entries/overlay -> find() = wire_required_field_
    // missing). Required so a default MessageView<Index>{} is well-formed —
    // the 2b cutover's dict::reify kEmpty sentinel + the seam #18
    // flyweight_shape_test default-construct it (T028).
    OffsetTable() = default;

    // Eagerly scans the frame's tag=value<SOH> stream into mr-backed
    // storage. On a DoS-cap breach the table is left empty and the breach
    // is reported by find()/build_status(). Likewise, if `mr` throws
    // bad_alloc mid-build it degrades the SAME way (empty table, status_ =
    // out_of_memory) — a noexcept ctor must not let bad_alloc escape and
    // std::terminate (004 T059 / Codex adversarial review).
    // The Config overload threads FR-015 / [2b §1.2] caller-tunable caps.
    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]]) noexcept;

    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]], void const* opaque_dict,
                group_member_fn_t group_member_fn,
                group_delim_fn_t group_delim_fn = nullptr) noexcept;

    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]], Config cfg) noexcept;

    OffsetTable(frame_view const& frame [[clang::lifetimebound]],
                std::pmr::memory_resource* mr [[clang::lifetimebound]], Config cfg,
                void const* opaque_dict, group_member_fn_t group_member_fn,
                group_delim_fn_t group_delim_fn = nullptr) noexcept;

    // Non-RED build status (ok, or the wire_* cap/format error hit).
    [[nodiscard]] core::expected_t<void> build_status() const noexcept { return status_; }

    [[nodiscard]] core::expected_t<entry> find(
        std::uint16_t tag) const noexcept;  // first occurrence, O(1)

    [[nodiscard]] std::span<entry const> entries() const noexcept [[clang::lifetimebound]] {
        return {entries_.data(), entries_.size()};
    }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

    // Lazily-built group sub-index spanning entries owned by the first
    // occurrence of `no_tag` (the count field).
    //
    // Dict-AWARE construction (Parser{dict} path — all production callers):
    //   Group extent is bounded by real dictionary membership: the group ends
    //   at the first entry whose tag the dict says is not a group member.
    //   This is the [2b §4.7]-conformant path (dictionary's first-field-of-
    //   group rule; cap = default_max_group_entries_per_instance).
    //
    // Dict-FREE construction (OffsetTable(frame, mr) / OffsetTable(frame, mr,
    //   Config), or any construction with a null group-membership predicate):
    //   group() DECLINES. It returns wire_required_field_missing for every
    //   no_tag — the same absent result the dict-aware path returns for a tag
    //   the dictionary does not know to be a group — so group_slices() yields
    //   an empty span and the C-ABI reports TYPE_MISMATCH (E-2 / CA-010-read).
    //   A repeating group is not a wire-recognisable structure: [2b §4.7]
    //   defines its boundary as the dictionary's first-field-of-group rule per
    //   [FIX50SP2 §3], so without a dictionary the boundary is UNDEFINED, not
    //   merely unavailable. Callers that need groups must construct through a
    //   dictionary (Parser{dict}).
    //
    //   This REPLACED a rest-of-message degradation (group_end =
    //   entries_.size()) that was carried as a [2b §4.7] deviation (D-I',
    //   .specify/decisions/004-wire-codec-completeness.md). That deviation is
    //   RETIRED, not re-waived: declining is conformant, because §4.7 says
    //   nothing about a path with no dictionary to consult. See fixpp#220 —
    //   the degradation absorbed trailing top-level fields into the last
    //   instance, which reached group_slices() and the typed group_view<GroupT>
    //   as a wrong extent, and let a tightened Config reject a valid frame.
    class group_index {
    public:
        group_index() = default;
        group_index(std::uint16_t no_tag, std::size_t first, std::size_t count) noexcept
            : no_tag_{no_tag}, first_{first}, count_{count} {}
        [[nodiscard]] std::uint16_t no_tag() const noexcept { return no_tag_; }
        [[nodiscard]] std::size_t first_entry() const noexcept { return first_; }
        [[nodiscard]] std::size_t entry_count() const noexcept { return count_; }

    private:
        std::uint16_t no_tag_ = 0;
        std::size_t first_ = 0;
        std::size_t count_ = 0;
    };

    [[nodiscard]] core::expected_t<group_index> group(std::uint16_t no_tag) const noexcept;  // lazy

    // 063 Defect B (T021): nesting-aware extent walk. Consumes the FULL byte
    // extent (all declared instances) of the group whose NumInGroup count
    // field sits at entries_ index `count_idx`, under membership context
    // `ctx` (the context in which THIS group's membership is registered).
    // Recurses into nested group counts (a member tag that itself heads a
    // group in-context) so a multi-entry nested group's repeated delimiter is
    // NOT mistaken for an outer-instance boundary (the pre-063 flat
    // `seen_in_instance` truncation bug). Approach A: reads each nested
    // group's DECLARED count and consumes exactly that many instances,
    // fail-closed (bounded by entries_.size(); declared-vs-actual mismatch is
    // the validator's job per plan.md). Alloc-free (stack-only index
    // recursion). On depth > kMaxGroupDepth or a per-level entry-cap breach,
    // sets `overflow = true` (caller returns err_group_too_large, T022).
    // Returns the entries_ index one-past the group's last field.
    [[nodiscard]] std::size_t consume_group_extent(
        std::size_t count_idx, group_context const& ctx, std::uint8_t depth,
        bool& overflow) const noexcept;

    // 063 T006: sets this table's stored context (msg_type + bounded
    // parent-no_tag path), read by group()'s membership predicate calls. The
    // ROOT table is seeded with `{msg_type, path=[]}` at `MessageView`
    // construction time (parser.hpp), before any group_slices() call this
    // parse; `MessageView::group<>()` re-applies the same root context
    // (idempotent) before its typed read. A NESTED sub-table is seeded once
    // by `build_nested_subview` at construction. Mutable/const like the
    // table's other lazily-set state (group_slices_/nested_cache_) — safe to
    // call repeatedly with the SAME value within one parse (context is
    // constant per parse, FR-005).
    void set_group_context(group_context const& ctx) const noexcept;

    // 065 T003: public seed accessor for a C-ABI group cursor's OWN context
    // (its container path + its own no_tag) — `stored_group_context().pushed(
    // no_tag)` (private helper `:257`, out-of-line def next to it in
    // offset_table.cpp — `group_context` is only forward-declared above, so
    // an inline `.pushed()` body would not compile). Used by
    // `fixpp_msg_get_group` to seed the top-level cursor's `group_ctx`
    // (research Decision 2; data-model.md §Invariant).
    [[nodiscard]] group_context group_context_for(std::uint16_t no_tag) const noexcept;

    // Repeating-group instance slices for `no_tag`, in document order,
    // materialized once into this table's per-message mr arena (append-only,
    // reserved once → no reallocation), then cached per no_tag. The returned
    // span is valid for the message lifetime and is NOT clobbered by another
    // group's access — replaces the prior static thread_local
    // (zero-alloc-after-build, no cross-view aliasing). Empty span if the
    // group is absent or the table is RED.
    [[nodiscard]] std::span<group_slice const> group_slices(std::uint16_t no_tag) const noexcept
        [[clang::lifetimebound]];

    // 073 T002/T003: INTERNAL status-bearing form of group_slices() above
    // (data-model.md §"Internal group_slices_result"; research.md §D2 mode
    // (b), FR-002). Only `nested_group_slices` consumes this. Sets
    // `alloc_failed = true` in the `catch (std::bad_alloc)` degrade path;
    // `false` on a warm cache hit or a genuine (possibly count-0) materialized
    // span. The public `group_slices()` above is unchanged and is a one-line
    // wrapper: `return group_slices_status(no_tag).slices;`.
    [[nodiscard]] group_slices_result group_slices_status(std::uint16_t no_tag) const noexcept
        [[clang::lifetimebound]];

    // [D5 deviation] Cross-layer getter: returns the memory_resource backing this
    // table's arena (the per-message PMR resource captured at construction time).
    // Used by the capi layer (fixpp_msg_get_group / fixpp_group_get_nested_group)
    // to allocate group cursor shells from the same dispatch-window arena as the
    // group_slices they reference, satisfying FR-002 / SC-003 (zero-global-heap
    // read path) — cursor reclaimed wholesale when the arena resets or destructs.
    [[nodiscard]] std::pmr::memory_resource* resource() const noexcept {
        return entries_.get_allocator().resource();
    }

    // [2b §4.7] 062 T006: single flat nested-subview cache, owned ONLY by
    // the ROOT OffsetTable (reached via entry_context.parent_cache_owner,
    // threaded UNCHANGED at every descent depth — nested sub-tables own no
    // cache of their own; data-model.md §"Nested sub-view cache", RC2/RC5,
    // INV-G3). Keyed by `(slice_data, nested_no_tag)` where `slice_data` is
    // the outer entry slice's globally-unique `data` pointer
    // (`outer_occurrence_id`), collision-free at every nesting depth because
    // every occurrence's slice is carved in place from the one parent frame
    // buffer. Build-once / fetch-cached: on a first request for a given
    // `(slice_data, nested_no_tag)` pair, builds a dict-aware sub-OffsetTable
    // over the slice via `build_nested_subview` (T005, RC1 — slice-scoped
    // `len+1`, the whole-frame `build()` guard and `group_slice.len` stay
    // UNCHANGED) and caches it in THIS table's own per-message arena; a
    // second distinct `nested_no_tag` over the SAME `slice_data` reuses the
    // already-built sub-table (one sub-OffsetTable indexes every entry in
    // the slice, so no rebuild is needed) — strictly fewer builds than
    // INV-G3's "at most one per key" bound. Repeat requests for the same
    // pair allocate zero (the returned span is served from the sub-table's
    // own already-cached `group_slices()`). Empty span if the group is
    // absent, the slice is null, or the sub-build failed (degrade, never
    // throw/UB — mirrors `group_slices()`'s own degradation contract).
    // 063 T008: `ctx` is the calling entry's OWN context (its container path
    // + its own no_tag, e.g. {msg_type,[296],1} for a QuoteSet entry) — it is
    // seeded VERBATIM as the new/reused sub-table's stored context (no
    // further push here: the sub-table answers `group(nested_no_tag)`, whose
    // key is `(ctx, nested_no_tag)` — `nested_no_tag` is a separate argument,
    // not part of the stored path). Ignored on a warm cache hit (a built
    // sub-table's context was already seeded once at cold-build time and is
    // invariant for the rest of this parse, FR-005).
    [[nodiscard]] nested_slices_result nested_group_slices(
        std::byte const* slice_data [[clang::lifetimebound]], std::size_t slice_len,
        std::uint16_t nested_no_tag, void const* opaque_dict, group_member_fn_t group_member_fn,
        detail::generation_token gen, group_context const& ctx) const noexcept
        [[clang::lifetimebound]];

    // 065 T004: convenience overload forwarding to the 7-arg
    // `nested_group_slices` above using THIS table's own `opaque_dict_` /
    // `group_member_fn_` and a build-mode-safe token (`token_for_nested_cache()`
    // `:private below` — `gen_` exists only `#ifndef NDEBUG`, so forwarding it
    // directly would not compile in release). The 7-arg algorithm + cache
    // keying stay UNTOUCHED (FR-005). Out-of-line in offset_table.cpp (needs
    // the complete `group_context` type, only forward-declared here — same
    // rule as `group_context_for()` above). Used by the C-ABI nested read
    // (research Decision 4; data-model.md §Reused).
    [[nodiscard]] nested_slices_result nested_group_slices(std::byte const* slice_data
                                                           [[clang::lifetimebound]],
                                                           std::size_t slice_len,
                                                           std::uint16_t nested_no_tag,
                                                           group_context const& ctx) const noexcept
        [[clang::lifetimebound]];

private:
    [[nodiscard]] static std::size_t overlay_cap_for(std::size_t n) noexcept;
    void build(frame_view const& frame) noexcept;  // shared build impl (both ctors)
    void check_alive() const noexcept;

    // 062 T005: dict-aware sub-view-over-slice builder. Placement-constructs
    // a sub-OffsetTable into `mr` (an arena-owned pointer; never freed
    // individually — reclaimed wholesale with the arena, same lifetime
    // contract as `group_slices_`) over the slice-scoped `{data, len+1}`
    // bytes via the dict-aware ctor (`opaque_dict`/`group_member_fn`
    // threaded — MANDATORY, INV-G7; the dict-free fallback is never taken on
    // this path). RC1: the terminal SOH at `data+len` is provably already
    // present in the parent frame buffer (the slice is interior to a
    // well-formed, checksum-terminated frame in which every field is
    // SOH-terminated), so widening THIS build's input span by one byte lets
    // a counted Length+Data last field pass the UNCHANGED whole-frame
    // `build()` guard (`offset_table.cpp:264-268`) without relaxing it and
    // without widening the shared `group_slice.len`. Returns nullptr on
    // allocation failure (degrade, never throw).
    // 063 T008: `ctx` seeds the new sub-table's stored context (via
    // set_group_context) immediately after construction — see
    // nested_group_slices()'s doc comment above for what `ctx` means.
    [[nodiscard]] static OffsetTable* build_nested_subview(
        std::byte const* data, std::size_t len, std::pmr::memory_resource* mr,
        void const* opaque_dict, group_member_fn_t group_member_fn, detail::generation_token gen,
        group_context const& ctx, group_delim_fn_t group_delim_fn = nullptr) noexcept;

    // 063 T006: builds an actual `group_context` value from the raw fields
    // below (needs the complete type — defined in offset_table.cpp, which
    // includes group_view.hpp).
    [[nodiscard]] group_context stored_group_context() const noexcept;

    // Tight upper bound on the total number of top-level group instances that
    // can ever be appended to `group_slices_` (summed over every top-level group
    // tag a caller may read): the sum of the DECLARED counts of the top-level
    // group count-fields, clamped to `entries_.size()`. Used to `reserve()`
    // `group_slices_` ONCE so it never reallocates (held spans stay valid) while
    // keeping the lazy materialization inside the fixed parse arena on
    // toolchains with larger STL metadata (MSVC release — PR #181 Tier-2
    // arena_fit; the loose `entries_.size()` reserve over-allocated ~3x and
    // exhausted the 16 KiB null-upstream arena there). See offset_table.cpp.
    [[nodiscard]] std::uint32_t group_slices_reserve_bound() const noexcept;

    static constexpr std::uint8_t kMaxGroupDepth = 16;  // mirror emit_messages.cpp:137

    std::byte const* frame_base_ = nullptr;  // for group_slice (ptr,len)
#ifndef NDEBUG
    detail::generation_token gen_{};
#endif

    // 065 T004: build-mode-safe token forward for the 4-arg
    // `nested_group_slices` convenience overload — `gen_` above exists only
    // `#ifndef NDEBUG`, so a release TU cannot name it directly.
    [[nodiscard]] detail::generation_token token_for_nested_cache() const noexcept {
#ifndef NDEBUG
        return gen_;
#else
        return {};
#endif
    }

    Config cfg_{};                           // caller-tunable caps (FR-015 / [2b §1.2])
    void const* opaque_dict_ = nullptr;
    group_member_fn_t group_member_fn_ = nullptr;
    // 083 T057 (C-8.1): supplied at EVERY site that supplies opaque_dict_.
    group_delim_fn_t group_delim_fn_ = nullptr;
    // 063 T006: raw storage for the stored group_context (msg_type + bounded
    // parent-no_tag path). Stored as constituent fields, NOT a `group_context`
    // member by value — `group_context` is only forward-declared in this
    // header (see the forward-decl comment above); storing it by value would
    // need the complete type here, which would require including
    // group_view.hpp and cycle back to THIS header. Set via
    // set_group_context() (mutable — same lazy-const-method idiom as
    // group_slices_/nested_cache_ below); default-empty on a table that never
    // calls it (dict-free ctors — group_member_fn_ is null there, so this
    // state is never read).
    mutable std::string_view group_ctx_msg_type_{};
    mutable std::array<std::uint16_t, kMaxGroupDepth> group_ctx_parent_path_{};
    mutable std::uint8_t group_ctx_depth_ = 0;
    std::pmr::vector<entry> entries_;
    // Open-address robin-hood overlay: slot value = index into entries_ + 1
    // (0 = empty). Holds the FIRST occurrence per tag.
    std::pmr::vector<std::uint32_t> overlay_;
    core::expected_t<void> status_;
    // Per-process overlay hash seed, snapshotted at build() and folded into
    // mix() by both build() and find() so the open-address slot for a tag is
    // unpredictable to a remote attacker — defeats the crafted-collision
    // false-absent (W-P3-2 / HashDoS). Per-TABLE cache (not a find()-hot magic
    // static). 0 on a default-constructed (never-built) table (unused: find()
    // returns absent before hashing when overlay_ is empty).
    std::uint32_t seed_ = 0;
    // Lazy mr-backed group slices. Append-only and reserved once to the
    // entry-count upper bound, so no push_back ever reallocates — every
    // span handed out stays valid for the message lifetime even when
    // several distinct groups are accessed and held simultaneously
    // (the prior static thread_local clobbered across calls; this does not).
    struct group_span {
        std::uint16_t no_tag;
        std::uint32_t start;  // index into group_slices_
        std::uint32_t count;
    };
    mutable std::pmr::vector<group_slice> group_slices_;
    mutable std::pmr::vector<group_span> group_index_;
    mutable bool group_slices_reserved_ = false;

    // 062 T006: single flat nested-subview cache row — see
    // `nested_group_slices()` above for the ownership/keying contract.
    // `table` is arena-allocated by `build_nested_subview` (T005) and never
    // individually freed (reclaimed with the arena).
    struct nested_cache_row {
        std::byte const* slice_data;
        std::uint16_t nested_no_tag;
        OffsetTable* table;
    };
    mutable std::pmr::vector<nested_cache_row> nested_cache_;

    // 073 T001 / gate-b/r1 FQ-2: TEST-ONLY nested_cache_ introspection seam,
    // forward-declared here for friendship only. Unlike frame_view_access /
    // frame_view_slice_access (framer.hpp:104-115), which are PRODUCTION
    // seams the parser/wire layer mints views through at runtime and so
    // cannot be gated, this friend is test-only (the definition itself says
    // "Never called from production code",
    // tests/support/wire_test_hooks.hpp:38) — it follows the repo's
    // FIXPP_TEST_HOOKS test-only-gating convention instead (see
    // session/file_store.hpp, session/seqnum_manager.hpp, session/session.hpp,
    // core/system_clock_source.hpp; [const §XV.9]). The DEFINITION lives in
    // tests/support/wire_test_hooks.hpp (never installed), so no test-only
    // accessor code ships in this public header, and the friendship itself is
    // now only granted to FIXPP_TEST_HOOKS builds. Resolves the sub-table
    // ALREADY built for a (slice_data, nested_no_tag) pair without triggering
    // a build — used by the wire-level primitive witness to pin research.md
    // §D2 mode (a)/(b)/(c) by introspecting the real sub-table rather than a
    // `sizeof(OffsetTable)`-tuned cap band (not portable across toolchains,
    // research.md "Platform-robust mode pinning").
    // 083 T056 (W-10): TEST-ONLY read of `group_slices_reserve_bound()`, same
    // seam and same gating as the sibling above — the DEFINITION lives in the
    // non-installed tests/support/wire_test_hooks.hpp, so this installed
    // header gains a friend DECLARATION and no accessor code. W-10 must assert
    // the reserve bound is UNCHANGED across a pre-083 / post-083 pair of
    // tables; there is no behavioural proxy for it (it is a reservation, not
    // an output), and a re-derivation in the test would assert the test's own
    // arithmetic rather than `:597`'s.
#ifdef FIXPP_TEST_HOOKS
    friend struct nested_cache_access_for_testing;
    friend struct reserve_bound_access_for_testing;
#endif  // FIXPP_TEST_HOOKS
};

}  // namespace fixpp::wire

// NOTE: the TEST/FUZZ-ONLY `detail::set_overlay_seed_for_testing` DECLARATION
// (W-P3-2) has moved to the non-installed `tests/support/wire_test_hooks.hpp`
// (Gate B PR #166 round-1 Finding 2b) so this installed public header exposes
// no test-only hook. The DEFINITION stays in `src/wire/offset_table.cpp`
// (external linkage unchanged).
