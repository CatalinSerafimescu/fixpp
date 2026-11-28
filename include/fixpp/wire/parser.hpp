#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/parser.hpp
// [2b §4.3] header-only template Parser<Mode> + MessageView<Mode> : View +
// field_iterator. Mode is resolved at COMPILE time (no runtime branch on the
// hot path, FR-003): access_mode::Index builds the OffsetTable eagerly;
// access_mode::Iter skips it (zero-alloc streaming; the standard constexpr
// Length+Data pair table always applies, and a dictionary's own pairs apply
// too when the view carries a bundle — `Parser<Iter>::parse_iter()` threads
// one, so Iter is NOT dict-free). Authority: .specify/2b-wire.md v0.2;
// shape oracle contracts/parser.hpp.
// 100-wire-group-read-integrity: `MessageView::group<>()` passes the read's
// status to its `group_view` (research D6), superseding 073's span-only top-level
// read (L-073-1).
//
// (U1) Every W-009 field type decodes/encodes strictly via the 2a
// decimal<T> / 2c dict::field_traits<...> boundary — the wire layer performs
// NO field decoding (FR-006). field_view::bytes() is the boundary; the
// 001-FLOAT accessor leg (T027) lives at that boundary, not in the parser.
// (C1) every call into a (potentially throwing) 2a/2c trait wrapper is
// fenced by core::detail::trap_throw so no exception escapes the noexcept
// parse->fromApp window (FR-013, [arch §5.3]).

#include <algorithm>
#include <cassert>   // detail::checked_owner precondition (fixpp#495)
#include <concepts>  // std::same_as (gate-b/r1 FQ-2 ctor constraint)
#include <cstddef>
#include <cstdint>
#include <expected>                        // std::unexpect
#include <fixpp/core/decimal_alias.hpp>    // fixpp::decimal_t (2a/001 trait)
#include <fixpp/core/decimal_helpers.hpp>  // core::detail::trap_throw (C1)
#include <fixpp/core/error.hpp>
// 066-dict-backed-inbound-parse T003: MessageView::membership_copy() (below)
// returns an OWNED table_view by value, needing it complete. table_view.hpp
// has a deliberately minimal include graph (no mutex, no heavy asio — see
// its own file header / validator.hpp's own identical §XV.9 confirmation).
// [arch §2.3]: wire -> dictionary is an explicitly allowed edge; no cycle
// (table_view.hpp includes nothing from wire/). MUST stay at file scope
// (outside `namespace fixpp::wire { ... }` below) — see the mid-namespace
// pitfall note at membership_copy()'s out-of-line definition.
#include <fixpp/dict/table_view.hpp>
#include <fixpp/dict/version_profile.hpp>  // 099: resolved_application_version()
#include <limits>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>

#include "dict_hooks.hpp"  // fixpp::wire::dict_hooks (fixpp#426)
#include "errors.hpp"      // wire::err_required_field_missing (062 T004)
#include "field_view.hpp"
#include "framer.hpp"
#include "group_view.hpp"
#include "offset_table.hpp"
#include "tag_scan.hpp"  // accumulate_tag_digit (SC-004 / 040-inbound-tag-overflow)
#include "unknown_fields.hpp"
#include "view.hpp"

// fixpp::dict::table_view is fully included above (066 T003:
// MessageView::membership_copy() returns it by value) — no forward
// declaration needed here anymore.

namespace fixpp::wire {

enum class access_mode : std::uint8_t { Iter, Index };

namespace detail {

// Standard-header tags used for msg_type/msg_seq_num lookups.
inline constexpr std::uint16_t tag_msg_type = 35;
inline constexpr std::uint16_t tag_msg_seq_num = 34;

// fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.2, owner ruling R-A):
// the tag that selects Parser's OWNED route. Constructible anywhere; living in
// `detail` is what marks it "not an entry point" (`.specify/api-contract.md`'s
// Internal class, which names `detail` tags such as this one). A caller naming it
// takes on the OWNER-OBJECT RULE stated at Parser's owned-route constructor.
struct owned_route_key {
    explicit owned_route_key() = default;
};

// Precondition of the owned route: a non-null owner, checked BEFORE the
// dereference.
inline fixpp::dict::table_view const& checked_owner(
    std::shared_ptr<const fixpp::dict::table_view> const& owner) noexcept {
    assert(owner);
    return *owner;
}

// Reaches MessageView's private shared_membership() (defined below).
struct message_view_membership_access;
// Writes MessageView's resolved application version (defined below).
struct message_view_resolved_access;

// W1 D-688-2 (`.specify/688-707-559-wire-headroom-token.md` §1.4a, ruling R-W1-6): the encoding of
// MessageView<Index>'s first-unknown index. The sentinel is "not computed yet"; a stored value v
// with v <= entries().size() is computed, v < size being the first unknown entry's index and
// v == size "none" (the end index next_unknown returns when it finds nothing).
inline constexpr std::uint32_t kFirstUnknownNotComputed = std::numeric_limits<std::uint32_t>::max();

// Read: whether v is a computed index for a table of `size` entries. The != half keeps the
// sentinel from reading as an index on a table of that many entries or more; the <= half makes a
// moved-from view, whose table no longer holds the entries its index counted, rescan.
[[nodiscard]] constexpr bool first_unknown_is_set(std::uint32_t v, std::size_t size) noexcept {
    return v != kFirstUnknownNotComputed && v <= size;
}

// Store: whether a found index is representable. One that is not is never stored, so the
// sentinel stays and that view rescans on every call.
[[nodiscard]] constexpr bool first_unknown_fits(std::size_t idx) noexcept {
    return idx < kFirstUnknownNotComputed;
}

// Where std::size_t is 32-bit the first sum wraps to 0 and this witness holds either way.
static_assert(!first_unknown_is_set(kFirstUnknownNotComputed,
                                    std::size_t{kFirstUnknownNotComputed} + 1U));
static_assert(!first_unknown_fits(kFirstUnknownNotComputed));
static_assert(first_unknown_fits(kFirstUnknownNotComputed - 1U));
static_assert(first_unknown_is_set(3, 3));
static_assert(!first_unknown_is_set(4, 3));

}  // namespace detail

template <access_mode Mode>
class Parser;  // befriended by MessageView; defined below with its default argument

template <access_mode Mode>
class MessageView : public View {
public:
    constexpr MessageView() noexcept = default;

    // Move-only (move-CONSTRUCTION only — see below). A MessageView COPY runs
    // std::pmr's select_on_container_copy_construction on the OffsetTable's
    // containers, which returns a DEFAULT-constructed polymorphic_allocator
    // (-> new_delete_resource) — silently re-rooting `table_`'s allocator off
    // the per-message arena. Its lazily-built nested sub-OffsetTables
    // (placement-new'd, reclaimed wholesale with the arena, never individually
    // freed — offset_table.cpp build_nested_subview) would then allocate from
    // the global heap and LEAK on every nested read. Forbid copy; move-
    // CONSTRUCTION preserves the source's arena allocator (move-construction
    // of a pmr container adopts the source allocator). Move-ASSIGNMENT is
    // deleted too: std::pmr::polymorphic_allocator does NOT propagate on
    // container move-assignment (propagate_on_container_move_assignment is
    // false_type), so `mv = std::move(parsed)` would keep `mv`'s (possibly
    // default-rooted) allocator and reopen the same leak class via a
    // different path — [gate-b/r1 RC#1]. [061 L1 / B-061-4]
    MessageView(MessageView const&) = delete;
    MessageView& operator=(MessageView const&) = delete;
    MessageView(MessageView&&) noexcept = default;
    MessageView& operator=(MessageView&&) = delete;

    // [2b §4.3] Construct with a dict_hooks bundle (fixpp#426, design §3).
    // The dictionary is borrowed from the caller; `hooks` aliases that
    // caller-owned object. No incomplete-type issues — dict_hooks uses
    // void const* + fn ptrs internally. ([PR68-02]/[PR68-10] fix.)

    // 384 / fixpp#426: `hooks` has NO default — see the same note on
    // `OffsetTable`'s dict-aware ctors (offset_table.hpp). A view carrying a
    // dictionary but no delimiter oracle must now say so at the call site.
    MessageView(frame_view const& frame, std::pmr::memory_resource* mr, dict_hooks hooks) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr, hooks},
          mr_{mr},
          hooks_{hooks} {
        // Gate B PR#176 r1 root cause #1: seed the ROOT group_context ({msg_type,
        // path=[]}) HERE, unconditionally, rather than lazily only in group<>()
        // below. Every root group read, whichever entry point makes it, must
        // agree on context so the no_tag-keyed cache (offset_table.cpp
        // group_index_) can never be poisoned by an unseeded call that runs
        // before the typed path seeds it (table_ is fully built by this point —
        // msg_type() reads via table_.find(35), valid post-construction).
        table_.set_group_context(group_context{.msg_type = msg_type()});
    }

    // FR-015 / [2b §1.2]: same as above but with caller-tunable caps.
    // Delegates to the reserving construction with no reserve, which seeds the same
    // root group_context as the sibling ctor above, for the reason given there.
    MessageView(frame_view const& frame, std::pmr::memory_resource* mr, OffsetTable::Config cfg,
                dict_hooks hooks) noexcept
        requires(Mode == access_mode::Index)
        : MessageView{reserve_tag{}, frame, mr, cfg, hooks, 0} {}

private:
    // 093-inbound-frame-dispositions (data-model E-3): the reserving construction,
    // reached only through Parser::parse's reserve overload (Parser is a friend). The
    // tag keeps it apart from every public constructor.
    struct reserve_tag {
        explicit reserve_tag() = default;
    };
    MessageView(reserve_tag /*tag*/, frame_view const& frame, std::pmr::memory_resource* mr,
                OffsetTable::Config cfg, dict_hooks hooks, std::size_t reserve_entries) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr, cfg, hooks, reserve_entries},
          mr_{mr},
          hooks_{hooks} {
        // The same root group_context seed as the public dict-aware ctors.
        table_.set_group_context(group_context{.msg_type = msg_type()});
    }

public:
    MessageView(frame_view const& frame, std::pmr::memory_resource* mr) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr},
          mr_{mr} {}

    explicit MessageView(frame_view const& frame) noexcept
        requires(Mode == access_mode::Iter)
        : View{frame.bytes().data(), frame.bytes().size(), frame.token()} {
    }  // [2b §6.4] thread real pool token

    // fixpp#426 (design §3, Gate B r8 P-1): the dictionary-backed Iter view. Without
    // it `Parser<Iter>` had nowhere to put the bundle it captured, so `parse_iter()`
    // built a dict-free view and a dictionary's own Length+Data pairs never reached
    // the streaming scanner — the standard table split those frames, the dictionary's
    // pairs did not. `hooks` aliases the caller-owned dictionary, exactly as in the
    // Index ctors above.
    // 100 (B-100-7): the view holds `hooks` itself, not a copy of the bundle, so
    // `*hooks` must outlive the view (`parse_iter()` passes its own, and is
    // lifetime-bound to the parser). A null `hooks` reads through no dictionary.
    MessageView(frame_view const& frame, dict_hooks const* hooks) noexcept
        requires(Mode == access_mode::Iter)
        : View{frame.bytes().data(), frame.bytes().size(), frame.token()},
          hooks_{hooks != nullptr ? hooks : &detail::no_dict_hooks} {}

    [[nodiscard]] std::string_view msg_type() const noexcept [[clang::lifetimebound]] {
        return field_string(detail::tag_msg_type);
    }
    [[nodiscard]] std::uint32_t msg_seq_num() const noexcept {
        auto b = field_bytes(detail::tag_msg_seq_num);
        return parse_bounded_u32(b);
    }

    // ---- Iter streaming (optionally dictionary-backed) ---------------------
    class field_iterator {
    public:
        struct field {
            std::uint16_t tag = 0;
            std::span<const std::byte> value;
        };
        // fixpp#426: `hooks` has NO default (mirrors OffsetTable's dict-aware
        // ctors) — every direct construction site must say which dictionary
        // (or `dict_hooks::none()`) governs the Length+Data split. An Iter
        // view's own `begin()`/`end()` below pass `hooks_`, which is `none()`
        // only when the view was built WITHOUT a bundle. ⚠️ Iter mode is NOT
        // dict-free: `Parser<Iter>::parse_iter()` threads its own `hooks_` into
        // the view it returns (Gate B r8 P-1 — it used to drop them, and the
        // test that claimed to cover the Iter path was walking a
        // `MessageView<Index>` iterator).
        // 100 (B-100-7): the iterator holds `hooks` itself, not a copy of the
        // bundle, so `*hooks` must outlive the iterator; a null `hooks` reads
        // through no dictionary. `begin()`/`end()` pass the view's own bundle.
        field_iterator(std::span<const std::byte> buf, std::size_t pos,
                       dict_hooks const* hooks) noexcept
            : buf_{buf}, pos_{pos}, hooks_{hooks != nullptr ? hooks : &detail::no_dict_hooks} {
            advance();
        }
        [[nodiscard]] field const& operator*() const noexcept { return cur_; }
        field_iterator& operator++() noexcept {
            pos_ = next_;
            advance();
            return *this;
        }
        [[nodiscard]] bool operator==(field_iterator const& o) const noexcept {
            // A done_ iterator is terminal and compares equal to end() (which is
            // done_) REGARDLESS of pos_. The malformed-stop paths in advance()
            // set done_ without advancing pos_ to buf.size(); without this a
            // range-for (`begin() != end()`) would never terminate on a
            // malformed frame — a latent Iter-API hang the Length+Data hardening
            // would otherwise re-trigger via its SOH-mismatch stop (W-P2-1/-2).
            if (done_ || o.done_) {
                return done_ == o.done_;
            }
            return pos_ == o.pos_;
        }

        // 092 (specs/092-garbled-frame-reject/data-model.md E-4): the fault
        // record. Read-only; what the iterator yields does not depend on it.
        // Sticky: it holds the first fault any advance() met; a later fault
        // does not replace it.
        [[nodiscard]] field_fault fault() const noexcept { return fault_; }
        // The Length tag when fault() == length_data_mismatch; else 0.
        [[nodiscard]] std::uint16_t fault_length_tag() const noexcept { return fault_length_tag_; }

    private:
        void advance() noexcept;
        // Keeps the first fault (E-4 "sticky"); `length_tag` is recorded only
        // with a length_data_mismatch, and only when it is that first fault.
        void record_fault(field_fault kind, std::uint16_t length_tag) noexcept {
            if (fault_ == field_fault::none) {
                fault_ = kind;
                fault_length_tag_ = length_tag;
            }
        }
        std::span<const std::byte> buf_;
        std::size_t pos_ = 0;
        std::size_t next_ = 0;
        field cur_{};
        bool done_ = false;
        field_fault fault_ = field_fault::none;
        std::uint16_t fault_length_tag_ = 0;
        // Length+Data carry: set when the just-yielded field was a Length
        // tag, so the next (Data) field is read by fixed length.
        std::uint16_t prev_data_tag_ = 0;
        std::uint32_t prev_data_len_ = 0;
        dict_hooks const* hooks_;
    };

    // fixpp#426: passes THIS view's own `hooks_` — `none()` on a dict-free view,
    // and on an Iter view built from the frame alone. `Parser<Iter>::parse_iter()`
    // builds one WITH hooks (Gate B r8 P-1), so a dictionary-backed streaming walk
    // splits by that dictionary's pairs too.
    [[nodiscard]] field_iterator begin() const noexcept [[clang::lifetimebound]] {
        return field_iterator{bytes(), 0, &hooks()};
    }
    [[nodiscard]] field_iterator end() const noexcept [[clang::lifetimebound]] {
        return field_iterator{bytes(), bytes().size(), &hooks()};
    }

    // ---- Index random access ---------------------------------------------
    [[nodiscard]] OffsetTable const& offsets() const noexcept
        [[clang::lifetimebound]] requires(Mode == access_mode::Index) { return table_; }

    template <std::uint16_t Tag>
    [[nodiscard]] core::expected_t<field_view> get() const noexcept
        [[clang::lifetimebound]] requires(Mode == access_mode::Index) { return get(Tag); }

    [[nodiscard]] core::expected_t<field_view> get(std::uint16_t tag) const noexcept
        [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
            auto e = table_.find(tag);
            if (!e) {
                return core::expected_t<field_view>{std::unexpect, e.error()};
            }
            return field_view_access::make(bytes().data() + e->offset, e->length, token());
        }

    // 004-authored 001 wire FLOAT-field accessor leg (D-17, FR-006 /
    // [2b §7.1]). The wire layer performs NO decoding: it hands the field's
    // raw bytes across the 2a trait-decode boundary to
    // fixpp::decimal_t::parse(span, mr). (C1) the trait call is fenced by
    // core::detail::trap_throw so a throwing custom FIXPP_DECIMAL_T trait
    // cannot escape the noexcept parse->fromApp window (FR-013, [arch §5.3]).
    [[nodiscard]] core::expected_t<fixpp::decimal_t> get_decimal(
        std::uint16_t tag, std::pmr::memory_resource* mr) const noexcept
        [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
            auto fv = get(tag);
            if (!fv) {
                return core::expected_t<fixpp::decimal_t>{std::unexpect, fv.error()};
            }
            auto span = fv->bytes();
            // trap_throw wraps the (possibly throwing) trait; result is
            // expected<expected<decimal_t>> — flatten it.
            auto wrapped = core::detail::trap_throw(
                [span, mr]() { return fixpp::decimal_t::parse(span, mr); });
            if (!wrapped) {
                return core::expected_t<fixpp::decimal_t>{std::unexpect, wrapped.error()};
            }
            return *wrapped;
        }

    template <std::uint16_t NoTag, class GroupT>
    [[nodiscard]] group_view<GroupT> group() const noexcept
        [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
            // Instance slices are materialized once into the OffsetTable's
            // per-message mr arena (delimiter-aware: each reappearance of the
            // group's first field starts a new occurrence, document order; the
            // dictionary-driven nested-group refinement is layered by 2c's
            // GroupT). group_view only borrows the arena span — no thread-local,
            // no cross-view aliasing, zero-alloc after the first build.
            //
            // 062 T007: thread the base entry_context every generated entry
            // needs to read its own fields (span/outer_occurrence_id are
            // per-entry — group_view::operator[] fills those in from the SAME
            // instance slice it borrows). parent_cache_owner = THIS root
            // OffsetTable; the field is `const OffsetTable*` because the only
            // method a nested descent ever reaches through it is the const
            // nested_group_slices() (which mutates only its own `mutable` cache).
            // 063 T007: the ROOT context — {msg_type, path=[]} — is this message's
            // own context (depth 0, plan.md Context-propagation mechanism). Set on
            // `table_` BEFORE the read so group()'s membership predicate
            // calls see it; the entry_context each returned entry carries gets the
            // context PUSHED with NoTag (its own container path + own no_tag), so
            // a later nested descent from one of these entries seeds the correct
            // sub-table context (offset_table.hpp/.cpp T008).
            group_context const root_ctx{.msg_type = msg_type()};
            table_.set_group_context(root_ctx);
            entry_context ctx{};
            ctx.mr = mr_;
            ctx.hooks = hooks();
            ctx.gen = token();
            ctx.parent_cache_owner = &table_;
            ctx.group_ctx = root_ctx.pushed(NoTag);
            // 100 (research D6): the read's status reaches the view, so a top-level
            // read that could not complete reports `read_failed()`.
            auto const r = table_.group_slices_status(NoTag);
            return group_view<GroupT>{r.slices, ctx, r.status};
        }

    // [2b §4.8] Contract: unknown_fields() uses the dict pointer threaded from the
    // Parser that constructed this MessageView — no caller-supplied argument.
    // ([PR68-02] fix — the dict is captured at Parser construction time.)
    // W1 (`.specify/688-707-559-wire-headroom-token.md` D-688-1, fixpp#688): the filtered view
    // of [2b §4.8]. It walks the offset table's entries as it is iterated and yields each one the
    // dictionary does not define (the classify hook, table_view::tag_defined since ruling
    // R-W1-3), framing tags 8/9/10 exempt. It allocates nothing, so no state of the parse resource
    // changes what it yields (it supersedes 093's catch, FR-015, which returned an empty view on
    // exhaustion). The view stores no list; this MessageView stores one index, where the first
    // unknown entry is: the first call over a non-empty table stores the index when it is
    // representable; otherwise it leaves the sentinel and rescans, and later calls start at a
    // stored index (D-688-2). With no classify hook (dict-free construction) every non-framing
    // tag is unknown. The view copies its context out of this MessageView, not a pointer to it;
    // the dictionary must outlive every iteration.
    [[nodiscard]] unknown_fields_view unknown_fields() const noexcept
        [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
            check_alive();  // [2b §6.4]: building the view reads the table and the frame
            auto const ents = table_.entries();
            unknown_fields_view::entry const* const begin = ents.data();
            unknown_fields_view::entry const* const end = begin + ents.size();
            // The guard comes before the empty-table rule, so a moved-from view reads its stale
            // index through the guard (first_unknown_'s comment).
            if (detail::first_unknown_is_set(first_unknown_, ents.size())) {
                return unknown_fields_view{
                    unknown_fields_view::scan_state{.end = end,
                                                    .base = data_ptr(),
                                                    .classify = hooks().classify_fn(),
                                                    .dict = hooks().opaque_dict(),
                                                    .msg_type = msg_type()},
                    begin + first_unknown_, token()};
            }
            auto const v = unknown_fields_view::scan(begin, end, data_ptr(), hooks().classify_fn(),
                                                     hooks().opaque_dict(), msg_type(), token());
            if (!ents.empty()) {  // nothing is written over an empty table
                auto const idx = static_cast<std::size_t>(v.first_ - begin);
                if (detail::first_unknown_fits(idx)) {
                    first_unknown_ = static_cast<std::uint32_t>(idx);
                }
            }
            return v;
        }

    // ⚠️ Superseded at both copy sites by shared_membership() below (fixpp#495,
    // `.specify/495-493-486-dict-reify-copy.md` §2.3): `fixpp_msg_clone` and the
    // `reify` factory now share an owned-route table instead of copying it. This
    // accessor stays public and unchanged for its other callers.
    //
    // 066-dict-backed-inbound-parse T003 (mechanism (b)): was the ONE internal
    // membership-copy accessor shared by `fixpp_msg_clone` and the `reify`
    // owning handle to propagate this view's dictionary membership into an
    // OWNED, independently-lifetimed `table_view` — safe to outlive the
    // source session/Dictionary (`table_view`'s copy ctor deep-copies its
    // owned tables; table_view.hpp's copy-ctor note, spans "stable for lifetime"
    // per its own accessor comments). Re-concretizes `hooks_.opaque_dict()` back to a
    // `table_view` (sound: every production dict-backed parse binds a real `table_view` —
    // data-model.md "Reify owning handle" accessor precondition). A
    // dict-free source (`hooks_.opaque_dict() == nullptr`) yields a
    // default-constructed (empty) copy, so the clone/reify correctly stays
    // dict-free — the correct degenerate case (contracts/inbound-parse.md
    // C4). Defined out-of-line below (mirrors this file's existing
    // out-of-line-in-header convention for `field_iterator::advance`).
    // gate-b/r1 FQ-1 (PR #181 round 1): NOT noexcept — the copy-construction
    // below can throw std::bad_alloc (table_view.hpp's own note, "copy may throw
    // on allocation failure"). A noexcept here would convert that catchable
    // throw into std::terminate BEFORE either production caller's catch runs
    // (src/capi/message_write.cpp `catch (...)` -> FIXPP_ERR_CAPI_CONFIG_INVALID;
    // src/dictionary/reify.cpp `catch (std::bad_alloc const&)` -> dict_reify_oom),
    // defeating the dedicated OOM error code and violating fail-closed.
    [[nodiscard]] fixpp::dict::table_view membership_copy() const;

    // 066-dict-backed-inbound-parse T007/T008: companion predicate to
    // membership_copy() — true iff THIS view is itself dict-backed
    // (`hooks_.opaque_dict()` non-null). A clone/reify propagation site MUST bind its
    // re-framed MessageView dict-backed ONLY when this is true: binding a
    // non-null opaque_dict at an (empty) copy from a genuinely dict-free
    // source would flip OffsetTable::group()/consume_group_extent from the
    // dict-free DECLINE (gated on pointer NULLITY, not table content) to the
    // membership walk over an empty table — NOT the "clone/reify stays
    // dict-free" degenerate case data-model.md / contracts/inbound-parse.md C4
    // requires.
    //
    // 220: this sentence used to end "...from the POSITIONAL dict-free
    // fallback", which no longer exists — group() declines dict-free. Note
    // what that costs the rationale and what it does not: for GROUPS the two
    // states are no longer distinguishable from outside (both yield absent,
    // hence TYPE_MISMATCH), so the predicate is no longer load-bearing THERE.
    // It remains load-bearing for the non-group reasons this method also
    // gates — field classification and unknown_fields(), which do read table
    // CONTENT and so do differ between "no dictionary" and "an empty one".
    [[nodiscard]] bool is_dict_backed() const noexcept { return hooks().opaque_dict() != nullptr; }

    // fixpp#426: this view's own dict_hooks bundle — `none()` only for a view
    // constructed without one (default/frame-only construction), NOT for every
    // Iter view: `Parser<Iter>::parse_iter()` passes its own bundle through
    // (Gate B r8 P-1). Exposed so a caller minting its own field_iterator
    // (e.g. a nested/C-ABI scan) can reuse the EXACT dictionary this view was
    // built with.
    [[nodiscard]] dict_hooks const& hooks() const noexcept [[clang::lifetimebound]] {
        if constexpr (Mode == access_mode::Index) {
            return hooks_;
        } else {
            return *hooks_;
        }
    }

    // 099-fixt-appl-version-validation (data-model E-5, FR-014): the application version the
    // session resolved for this inbound message. `Unknown` on a non-FIXT session, an admin message,
    // an unresolved value, and any view that was not an inbound dispatch, which every Iter view is.
    [[nodiscard]] fixpp::dict::application_version resolved_application_version() const noexcept {
        if constexpr (Mode == access_mode::Index) {
            return resolved_application_version_;
        } else {
            return fixpp::dict::application_version::Unknown;
        }
    }

private:
    // fixpp#495: Parser seats `dict_owner_`; the accessor reaches shared_membership().
    template <access_mode>
    friend class Parser;
    friend struct detail::message_view_membership_access;
    friend struct detail::message_view_resolved_access;

    // fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.3): the table this
    // view was parsed against, as a refcounted owner. Owned route (dict_owner_ still
    // names the table in hooks_): the owner itself, no allocation. Borrowed and
    // dict-backed: a self-contained copy of the table, made in place (may throw
    // bad_alloc, like membership_copy()). Dict-free: nullptr. The identity check
    // makes an owner reassigned after the parse fall to the copy of the table the
    // view was parsed against; it does not make a dead owner safe (§3.1).
    [[nodiscard]] std::shared_ptr<const fixpp::dict::table_view> shared_membership() const;

    [[nodiscard]] std::span<const std::byte> field_bytes(std::uint16_t tag) const noexcept {
        if constexpr (Mode == access_mode::Index) {
            auto e = table_.find(tag);
            if (!e) {
                return {};
            }
            return {bytes().data() + e->offset, e->length};
        } else {
            for (auto it = begin(); !(it == end()); ++it) {
                if ((*it).tag == tag) {
                    return (*it).value;
                }
            }
            return {};
        }
    }
    [[nodiscard]] std::string_view field_string(std::uint16_t tag) const noexcept {
        auto b = field_bytes(tag);
        return {reinterpret_cast<char const*>(b.data()), b.size()};
    }

    struct empty_t {};
    [[no_unique_address]] std::conditional_t<Mode == access_mode::Index, OffsetTable, empty_t>
        table_{};
    // Index-mode extras.
    std::pmr::memory_resource* mr_ = std::pmr::null_memory_resource();
    // [2b §4.3] / [2b §4.8] fixpp#426: the dict_hooks bundle threaded from the
    // Parser (replaces the separate opaque_dict_/classify_fn_/
    // group_member_fn_/group_delim_fn_ fields). Default `none()` = dict-free
    // path (all non-framing = unknown). Threaded into every entry_context
    // minted by group<>() below (the dict-driven predicates a nested descent
    // needs to build a dict-aware sub-OffsetTable). ([PR68-02] fix.)
    // 100 (B-100-7): an Index view holds the bundle by value; an Iter view holds
    // a pointer to its parser's bundle, or to `detail::no_dict_hooks`, so
    // `parse_iter()` stores one pointer rather than the whole bundle.
    using hooks_holder_t =
        std::conditional_t<Mode == access_mode::Index, dict_hooks, dict_hooks const*>;
    static constexpr hooks_holder_t no_hooks() noexcept {
        if constexpr (Mode == access_mode::Index) {
            return dict_hooks::none();
        } else {
            return &detail::no_dict_hooks;
        }
    }
    hooks_holder_t hooks_{no_hooks()};
    // 099 (data-model E-5): Index mode only, as `dict_owner_` is, so an Iter view stores nothing.
    struct no_resolved_version_t {};
    [[no_unique_address]] std::conditional_t<Mode == access_mode::Index,
                                             fixpp::dict::application_version,
                                             no_resolved_version_t> resolved_application_version_{};
    // W1 D-688-2 (`.specify/688-707-559-wire-headroom-token.md` §1.4a, ruling R-W1-6): where the
    // first unknown entry is, as an index into table_.entries(), so a later unknown_fields() call
    // starts there instead of rescanning. Index mode only; an Iter view carries an empty member
    // whose type must stay distinct from the other empty members' types, because two
    // [[no_unique_address]] members of one empty type cannot share an address.
    //   - The encoding is detail::kFirstUnknownNotComputed ("not computed yet") or a computed
    //     index, "none" being the end index. detail::first_unknown_is_set reads it: its != half
    //     keeps the sentinel from reading as an index, its <= size half makes a moved-from view
    //     (which keeps the index but not the entries) rescan. detail::first_unknown_fits bounds
    //     the store: an index that is not representable is not stored, and that view rescans.
    //   - unknown_fields() tests the guard before its empty-table rule, so a moved-from view's
    //     stale index is read through the guard. It never writes the index over an empty table:
    //     owning_message_handle::view() returns one shared empty view on every thread.
    //   - The first call over a non-empty table stores the index when it is representable;
    //     otherwise it leaves the sentinel and rescans. A store is a write to a const view, under
    //     [2b §6.6]'s contract: one view, one thread at a time, as group<>()'s context writes are.
    //   - Declared right after resolved_application_version_ so that it falls in the padding
    //     before dict_owner_; re-derive with `-Xclang -fdump-record-layouts-complete` (clang) or
    //     `/d1reportSingleClassLayoutMessageView` (MSVC).
    //   - The initializer is mode-dependent (not_computed()), as hooks_'s is: {} would read as
    //     "computed, at index 0".
    struct no_first_unknown_t {};
    using first_unknown_t =
        std::conditional_t<Mode == access_mode::Index, std::uint32_t, no_first_unknown_t>;
    static constexpr first_unknown_t not_computed() noexcept {
        if constexpr (Mode == access_mode::Index) {
            return detail::kFirstUnknownNotComputed;
        } else {
            return no_first_unknown_t{};
        }
    }
    [[no_unique_address]] mutable first_unknown_t first_unknown_{not_computed()};
    // fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.1): the owner object
    // of the table in `hooks_` when this view came from Parser's owned route, else
    // nullptr. A BORROWED pointer to the shared_ptr object, not a shared_ptr: no
    // refcount traffic per inbound message. Read only by shared_membership().
    // Index mode only: both copy sites take a MessageView<Index>, so an Iter view
    // carries an empty member of its own type and stores no owner pointer.
    struct no_dict_owner_t {};
    [[no_unique_address]] std::conditional_t<Mode == access_mode::Index,
                                             std::shared_ptr<const fixpp::dict::table_view> const*,
                                             no_dict_owner_t> dict_owner_{};
};

// field_iterator::advance — honours `hooks_` (default `none()`, the standard
// table alone) so a Data field carrying embedded SOH is delimited by its
// Length field, including a dictionary's own custom pairs when `hooks_` was
// built `for_table_view()` (fixpp#426, design §3).
//
// 092 (data-model E-4): every stop and every tolerance below that
// `OffsetTable::build` would reject writes the fault record; none of them
// changes what is yielded. To re-derive the rows, read each `done_ = true`,
// each early `return` and the empty-tag fall-through below, and match each to
// an encoding-failure writer in `OffsetTable::build`.
template <access_mode Mode>
void MessageView<Mode>::field_iterator::advance() noexcept {
    constexpr std::byte SOH{0x01};
    constexpr std::byte EQ{static_cast<std::byte>('=')};
    if (pos_ >= buf_.size()) {
        done_ = true;
        cur_ = field{};
        return;
    }
    std::size_t i = pos_;
    std::uint32_t tag = 0;
    while (i < buf_.size() && buf_[i] != EQ && buf_[i] != SOH) {
        auto c = static_cast<unsigned char>(buf_[i]);
        if (c < '0' || c > '9') {
            record_fault(field_fault::malformed_tag, 0);  // E-4 S1
            done_ = true;
            return;
        }
        if (!fixpp::wire::accumulate_tag_digit(tag, c)) {
            record_fault(field_fault::malformed_tag, 0);  // E-4 S2
            done_ = true;
            return;
        }
        ++i;
    }
    if (i >= buf_.size() || buf_[i] != EQ) {
        record_fault(field_fault::malformed_tag, 0);  // E-4 S3
        done_ = true;
        return;
    }
    if (i == pos_) {
        record_fault(field_fault::malformed_tag, 0);  // E-4 T1: yielded as tag 0
    }
    ++i;  // over '='
    std::size_t vstart = i;

    // Capture and CONSUME the Length→Data carry up-front: it applies ONLY to the
    // field immediately following its Length tag, so clearing it here prevents a
    // non-adjacent later field inheriting a stale count (W-P2-1b).
    std::uint16_t const carry_tag = prev_data_tag_;
    std::uint32_t const carry_len = prev_data_len_;
    // While the carry is armed, cur_ still holds the Length field that armed it.
    std::uint16_t const carry_length_tag = cur_.tag;
    prev_data_tag_ = 0;
    prev_data_len_ = 0;

    // Length+Data: if the PREVIOUS field was a Length tag naming THIS Data tag,
    // its length is fixed (value may contain SOH). Iter is a best-effort,
    // no-error-channel convenience API (production ingest uses Index): an
    // over-long declared length clamps to the frame end, and a length that lands
    // on a non-SOH boundary stops iteration (there is no way to signal reject).
    if (carry_tag != 0 && static_cast<std::uint16_t>(tag) == carry_tag) {
        std::size_t const avail = buf_.size() - vstart;  // vstart <= size always
        if (carry_len > avail) {
            // Declared length exceeds the frame: best-effort clamp to the end
            // (subtraction bound => no size_t wrap on any width, W-P2-1a).
            record_fault(field_fault::length_data_mismatch, carry_length_tag);  // E-4 T2
            cur_ = field{static_cast<std::uint16_t>(tag), buf_.subspan(vstart, avail)};
            next_ = buf_.size();
            return;
        }
        std::size_t const end = vstart + carry_len;  // <= size, no wrap
        if (end < buf_.size() && buf_[end] != SOH) {
            // Non-SOH boundary: the declared length did not land on a field
            // boundary. No error channel in Iter mode → stop (W-P2-1a).
            record_fault(field_fault::length_data_mismatch, carry_length_tag);  // E-4 S4
            done_ = true;
            return;
        }
        if (end == buf_.size()) {
            // Nothing terminates the counted value; yielded as is.
            record_fault(field_fault::length_data_mismatch, carry_length_tag);  // E-4 T3
        }
        cur_ = field{static_cast<std::uint16_t>(tag), buf_.subspan(vstart, carry_len)};
        next_ = (end < buf_.size()) ? end + 1 : end;  // step over verified SOH
        return;
    }

    while (i < buf_.size() && buf_[i] != SOH) {
        ++i;
    }
    cur_ = field{static_cast<std::uint16_t>(tag), buf_.subspan(vstart, i - vstart)};
    next_ = (i < buf_.size()) ? i + 1 : i;

    if (std::uint16_t dt = hooks_->data_tag_for_length(static_cast<std::uint16_t>(tag)); dt != 0) {
        prev_data_tag_ = dt;
        prev_data_len_ = parse_bounded_u32(cur_.value);
    }
}

// 066-dict-backed-inbound-parse T003: MessageView<Mode>::membership_copy()
// out-of-line definition (mirrors field_iterator::advance's out-of-line-in-
// header placement above). `table_view` is complete via the top-level
// `#include <fixpp/dict/table_view.hpp>` above ([arch §2.3]: wire ->
// dictionary is an explicitly allowed edge; no cycle — table_view.hpp does
// not include anything from wire/). NOTE: that include must stay OUTSIDE
// `namespace fixpp::wire { ... }` — placing it here (mid-namespace) would
// nest <unordered_set>'s `namespace std` under `fixpp::wire::std`.
template <access_mode Mode>
fixpp::dict::table_view MessageView<Mode>::membership_copy() const {
    if (hooks().opaque_dict() == nullptr) {
        return fixpp::dict::table_view{};
    }
    // Copy-constructs (deep-copies the owned tables, table_view.hpp's copy-ctor note)
    // — the result is self-contained and outlives the source session/
    // Dictionary/table_view.
    return *static_cast<fixpp::dict::table_view const*>(hooks().opaque_dict());
}

// fixpp#495: MessageView<Mode>::shared_membership() — see the declaration.
template <access_mode Mode>
std::shared_ptr<const fixpp::dict::table_view> MessageView<Mode>::shared_membership() const {
    if (hooks().opaque_dict() == nullptr) {
        return nullptr;
    }
    if constexpr (Mode == access_mode::Index) {
        if (dict_owner_ != nullptr && dict_owner_->get() == hooks().opaque_dict()) {
            return *dict_owner_;
        }
    }
    // Copied in place from the table reference: control block and table share one
    // allocation, and no table_view is moved.
    return std::make_shared<const fixpp::dict::table_view>(
        *static_cast<fixpp::dict::table_view const*>(hooks().opaque_dict()));
}

namespace detail {
// fixpp#495: the only door to MessageView::shared_membership(). Used by the two
// copy sites (dict::reify's factory, fixpp_msg_clone).
struct message_view_membership_access {
    template <access_mode M>
    [[nodiscard]] static std::shared_ptr<const fixpp::dict::table_view> shared_membership(
        MessageView<M> const& v) {
        return v.shared_membership();
    }
};
// 099 (data-model E-5): the one door to the byte. Only the session's dispatch and the two copy
// writers E-5 names go through it.
struct message_view_resolved_access {
    static void set(MessageView<access_mode::Index>& v,
                    fixpp::dict::application_version version) noexcept {
        v.resolved_application_version_ = version;
    }
};
}  // namespace detail

// [2b §4.3] span-scan → token-bearing field_view helper (062 T004, N1). The
// one wire primitive that did not exist yet: reuses the field_iterator to
// locate `tag` within an arbitrary in-frame slice (e.g. a repeating-group
// entry's own bytes) and mints a field_view carrying the caller-supplied
// generation token via field_view_access::make — mirrors
// MessageView<Index>::get(tag) (above) minus the OffsetTable. No
// sub-index, zero heap allocation; tolerates a missing final SOH (the
// underlying field_iterator::advance() above already falls through end==size).
// On a miss, returns the SAME field-not-found error
// MessageView::get returns (wire_required_field_missing, via table_.find).
//
// fixpp#426 (design §3): `hooks` governs the Length+Data split within this
// slice — generated group-entry readers pass their entry_context's own
// `hooks` (a dictionary's custom pair, or `none()` for the standard table
// alone). The 3-arg overload below delegates with `none()`, unchanged for
// every dict-free caller.
[[nodiscard]] inline core::expected_t<field_view> get(std::span<const std::byte> span
                                                      [[clang::lifetimebound]],
                                                      std::uint16_t tag, dict_hooks const& hooks,
                                                      detail::generation_token gen) noexcept {
    using iter_t = MessageView<access_mode::Iter>::field_iterator;
    for (iter_t it{span, 0, &hooks}, end{span, span.size(), &hooks}; !(it == end); ++it) {
        if ((*it).tag == tag) {
            auto const& f = *it;
            return field_view_access::make(f.value.data(), f.value.size(), gen);
        }
    }
    return err_required_field_missing<field_view>();
}

[[nodiscard]] inline core::expected_t<field_view> get(std::span<const std::byte> span
                                                      [[clang::lifetimebound]],
                                                      std::uint16_t tag,
                                                      detail::generation_token gen) noexcept {
    return get(span, tag, dict_hooks::none(), gen);
}

// fixpp#426 (design §3): `dict_hooks::for_table_view` — defined here, not in
// dict_hooks.hpp, because it needs both `fixpp::dict::table_view` and
// `group_context` complete (dict_hooks.hpp only forward-declares
// `group_context` to avoid an include-graph cycle; see that header's own
// note). `inline` because this header is included by many TUs and the
// definition lives out-of-line from the class body.
inline dict_hooks dict_hooks::for_table_view(fixpp::dict::table_view const& dict) noexcept {
    return dict_hooks{
        std::addressof(dict),
        // W1 (`.specify/688-707-559-wire-headroom-token.md` ruling R-W1-3): "known" is "the
        // dictionary defines the tag", as [2b §4.8] specifies, not "this MsgType declares it";
        // a defined tag the MsgType does not declare is the validator's wire_unexpected_tag.
        // The MsgType parameter stays in classify_fn_t and is unused here.
        [](void const* d, std::string_view /*mt*/, std::uint16_t t) noexcept -> bool {
            return static_cast<fixpp::dict::table_view const*>(d)->tag_defined(t);
        },
        // 063 T015: resolves via the stored `group_context` (msg_type +
        // bounded parent-no_tag path) — the context-scoped membership key
        // (data-model.md "GroupMembership", Option A). Fixes Defect A: a
        // reused NumInGroup tag (e.g. FIX44 295) now resolves to the members
        // it has in THIS message/parent-path, not whichever variant the
        // loader saw first. 100 (research D2 "Lookup order"): the members come
        // from `wire_group_member_tags`, which answers a `group_ctx_` miss from the
        // occurrence's own record before the bare store.
        [](void const* d, group_context const& ctx, std::uint16_t no_tag,
           std::uint16_t tag) noexcept -> bool {
            auto const members =
                static_cast<fixpp::dict::table_view const*>(d)->wire_group_member_tags(
                    ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth},
                    no_tag);
            return std::ranges::any_of(
                members, [tag](std::uint16_t const member_tag) { return member_tag == tag; });
        },
        // 083 T057 (C-8.1): the delimiter sibling of the membership lambda,
        // resolving through the SAME opaque_dict and the SAME context key, by the
        // same lookup order (100).
        [](void const* d, group_context const& ctx,
           std::uint16_t no_tag) noexcept -> std::uint16_t {
            return static_cast<fixpp::dict::table_view const*>(d)->wire_group_first_field(
                ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth},
                no_tag);
        },
        // fixpp#426 (design §3): the Length+Data pairing sibling — resolves
        // through the SAME opaque_dict. Installed only when this dictionary declares
        // a pair whose two tags the standard table both leave unnamed: those are the
        // only pairs the lookup rule can honour, so otherwise the callback would
        // answer 0 for every field it was asked about, at a lookup each.
        dict.has_nonstandard_pair()
            ? +[](void const* d, std::uint16_t tag,
                  dict_hooks::pair_side from) noexcept -> std::uint16_t {
                  auto const* tv = static_cast<fixpp::dict::table_view const*>(d);
                  return from == dict_hooks::pair_side::length ? tv->length_pair_data_tag(tag)
                                                               : tv->data_pair_length_tag(tag);
              }
            : static_cast<dict_hooks::length_pair_fn_t>(nullptr),
        // 100 (research D2): the registration resolver, always installed, through the
        // SAME opaque_dict and the SAME context key. It names the definition, so a read
        // makes one lookup (T071 fix): the delimiter and members it returns are the
        // answers of the two lambdas above at the same key (`wire_group_resolve`).
        [](void const* d, group_context const& ctx,
           std::uint16_t no_tag) noexcept -> dict_hooks::group_resolution {
            auto const r = static_cast<fixpp::dict::table_view const*>(d)->wire_group_resolve(
                ctx.msg_type, std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth},
                no_tag);
            return dict_hooks::group_resolution{.registered = r.registered,
                                                .definition_known = true,
                                                .delimiter = r.delimiter,
                                                .members = r.members};
        }};
}

template <access_mode Mode = access_mode::Index>
class Parser {
public:
    Parser() noexcept = default;

    template <class TV>
    // dict_metadata is lvalue-constrained and only address-taken (inside
    // dict_hooks::for_table_view); forwarding would be wrong, so
    // missing-std-forward is a false positive here.
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    explicit Parser(TV&& dict_metadata) noexcept
        // gate-b/r1 FQ-2 (PR #181 round 1): constrained to fixpp::dict::table_view
        // (not merely any lvalue-referenced duck-typed dict). membership_copy()
        // unconditionally static_cast<table_view const*>(opaque_dict()) --
        // sound by construction only if every dict-backed Parser<> is built over
        // a real table_view. Census (src/+tests/+bench/): every construction site
        // already passes a table_view (tests/support/mock_dict_table.hpp is a
        // compatibility shim over table_view, not a distinct type).
        requires(std::is_lvalue_reference_v<TV &&> &&
                 std::same_as<std::remove_cvref_t<TV>, fixpp::dict::table_view>)
        // fixpp#426 (design §3): the three lambdas (classify, group_member,
        // group_delim) that used to live here move into
        // `dict_hooks::for_table_view`, defined below once `table_view` and
        // `group_context` are both complete — see dict_hooks.hpp's own note.
        : hooks_{dict_hooks::for_table_view(dict_metadata)} {}

    template <class TV>
    explicit Parser(TV&&) noexcept
        requires(!std::is_lvalue_reference_v<TV &&>)
    = delete;

    // fixpp#495 (`.specify/495-493-486-dict-reify-copy.md` §2.2): the OWNED route.
    // Views this parser returns record `&owner`, so a copy of them (reify, clone)
    // shares the table instead of deep-copying it. `owner` must be a non-null lvalue
    // of exactly `shared_ptr<const table_view>` (its address is stored, so a
    // temporary would dangle).
    //
    // OWNER-OBJECT RULE (§3.1; stated here once — each owner site points here):
    // the `shared_ptr` OBJECT passed as `owner` is seated once before any view is
    // parsed through it, is never reassigned or reset while such a view may be
    // shared, does not relocate, and outlives — so is destroyed after — every view
    // parsed through it. The pointee table is not enough: views read `&owner`.
    template <class SP>
    // `owner` is address-taken, never forwarded.
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    Parser(detail::owned_route_key /*key*/, SP&& owner) noexcept
        requires(std::is_lvalue_reference_v<SP &&> &&
                 std::same_as<std::remove_cvref_t<SP>,
                              std::shared_ptr<const fixpp::dict::table_view>>)
        : hooks_{dict_hooks::for_table_view(detail::checked_owner(owner))},
          owner_{std::addressof(owner)} {}

    // Redundant with the constraint above (each alone rejects rvalues); kept for
    // its clearer "deleted function" diagnostic.
    template <class SP>
    Parser(detail::owned_route_key /*key*/, SP&&) noexcept
        requires(!std::is_lvalue_reference_v<SP &&>)
    = delete;

    Parser(Parser const&) = delete;
    Parser& operator=(Parser const&) = delete;
    Parser(Parser&&) = delete;
    Parser& operator=(Parser&&) = delete;

    // Index only: `MessageView<Iter>` has no arena-taking ctor, so an Iter
    // instantiation was ill-formed rather than merely unused (Gate B r8 P-1).
    // `parse_iter()` is the Iter entry point.
[[nodiscard]] core::expected_t<MessageView<Mode>> parse(frame_view const& frame
                                                        [[clang::lifetimebound]],
                                                        std::pmr::memory_resource* mr) noexcept
    [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
        // Thread the dict_hooks bundle into the MessageView (fixpp#426).
        MessageView<Mode> mv{frame, mr, hooks_};
        mv.dict_owner_ = owner_;  // fixpp#495: nullptr unless built on the owned route
        if constexpr (Mode == access_mode::Index) {
            if (auto s = mv.offsets().build_status(); !s) {
                return core::expected_t<MessageView<Mode>>{std::unexpect, s.error()};
            }
        }
        return mv;
    }

// FR-015 / [2b §1.2] caller-tunable DoS caps: same contract as parse(),
// but threads an OffsetTable::Config so the per-instance group cap is
// tunable through the public Parser API (not collapsed to constants).
[[nodiscard]] core::expected_t<MessageView<Mode>> parse(frame_view const& frame
                                                        [[clang::lifetimebound]],
                                                        std::pmr::memory_resource* mr,
                                                        OffsetTable::Config cfg) noexcept
    [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
        // 083 T057 (C-8.1) / fixpp#426: the cap-tunable overload threads
        // `hooks_` too, through the reserve overload below. Omitting it there would
        // silently take C-8.4's dict-FREE fallback (wire-derived `entries_[first].tag`)
        // on a dictionary-backed parse — the missed construction site T057 warns
        // about, one API surface over.
        return parse(frame, mr, cfg, 0);
    }

// 093-inbound-frame-dispositions (data-model E-3): as above, and the offset table's
// entries are reserved once, up front, for `reserve_entries` fields (at most
// cfg.max_offset_entries). A per-call hint the table does not store: a clone or reify of
// the view re-parses from config() and reserves nothing. A reserve the resource cannot
// serve is reported as out_of_memory.
[[nodiscard]] core::expected_t<MessageView<Mode>> parse(frame_view const& frame
                                                        [[clang::lifetimebound]],
                                                        std::pmr::memory_resource* mr,
                                                        OffsetTable::Config cfg,
                                                        std::size_t reserve_entries) noexcept
    [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
        MessageView<Mode> mv{
            typename MessageView<Mode>::reserve_tag{}, frame, mr, cfg, hooks_, reserve_entries};
        mv.dict_owner_ = owner_;  // fixpp#495: nullptr unless built on the owned route
        if (auto s = mv.offsets().build_status(); !s) {
            return core::expected_t<MessageView<Mode>>{std::unexpect, s.error()};
        }
        return mv;
    }

[[nodiscard]] core::expected_t<MessageView<access_mode::Iter>> parse_iter(
    frame_view const& frame [[clang::lifetimebound]]) noexcept
    [[clang::lifetimebound]] requires(Mode == access_mode::Iter) {
        // Gate B r8 P-1: thread THIS parser's bundle. Returning `{frame}` here dropped
        // the dictionary this parser was constructed with, silently.
        // fixpp#495: an Iter view records no owner (MessageView's `dict_owner_`).
        return MessageView<access_mode::Iter>{frame, &hooks_};
    }

private : dict_hooks hooks_ {};  // fixpp#426: replaces the separate opaque_dict_/
                                 // classify_fn_/group_member_fn_/group_delim_fn_
                                 // fields.
    // fixpp#495: the owned route's owner object (§2.2), nullptr on every other route.
    // Held in both modes so the owned-route constructor compiles for either; only
    // Index-mode parses store it on a view.
    std::shared_ptr<const fixpp::dict::table_view> const* owner_ = nullptr;
};

}  // namespace fixpp::wire
