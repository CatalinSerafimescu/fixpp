#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/parser.hpp
// [2b §4.3] header-only template Parser<Mode> + MessageView<Mode> : View +
// field_iterator. Mode is resolved at COMPILE time (no runtime branch on the
// hot path, FR-003): access_mode::Index builds the OffsetTable eagerly;
// access_mode::Iter skips it (zero-alloc, dict-free streaming + a static
// constexpr Length+Data pair table). Authority: .specify/2b-wire.md v0.2;
// shape oracle contracts/parser.hpp.
//
// (U1) Every W-009 field type decodes/encodes strictly via the 2a
// decimal<T> / 2c dict::field_traits<...> boundary — the wire layer performs
// NO field decoding (FR-006). field_view::bytes() is the boundary; the
// 001-FLOAT accessor leg (T027) lives at that boundary, not in the parser.
// (C1) every call into a (potentially throwing) 2a/2c trait wrapper is
// fenced by core::detail::trap_throw so no exception escapes the noexcept
// parse->fromApp window (FR-013, [arch §5.3]).

#include <algorithm>
#include <concepts>  // std::same_as (gate-b/r1 FQ-2 ctor constraint)
#include <cstddef>
#include <cstdint>
#include <expected>                        // std::unexpect
#include <fixpp/core/decimal_alias.hpp>    // fixpp::decimal_t (2a/001 trait)
#include <fixpp/core/decimal_helpers.hpp>  // core::detail::trap_throw (C1)
#include <fixpp/core/error.hpp>
#include <fixpp/core/pmr_arena_upstream.hpp>  // detail::arena_upstream (MSVC-debug proxy)
// 066-dict-backed-inbound-parse T003: MessageView::membership_copy() (below)
// returns an OWNED table_view by value, needing it complete. table_view.hpp
// has a deliberately minimal include graph (no mutex, no heavy asio — see
// its own file header / validator.hpp:23,41's identical confirmation).
// [arch §2.3]: wire -> dictionary is an explicitly allowed edge; no cycle
// (table_view.hpp includes nothing from wire/). MUST stay at file scope
// (outside `namespace fixpp::wire { ... }` below) — see the mid-namespace
// pitfall note at membership_copy()'s out-of-line definition.
#include <fixpp/dict/table_view.hpp>
#include <memory>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>

#include "errors.hpp"  // wire::err_required_field_missing (062 T004)
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

// Static, dict-free Length+Data pairs ([FIX50SP2 §3]) for the Iter path so a
// Data field's value (which may contain SOH) is read by the preceding
// Length field, with no runtime dictionary. (length_tag -> data_tag)
struct len_data_pair {
    std::uint16_t length_tag;
    std::uint16_t data_tag;
};
inline constexpr len_data_pair length_data_table[] = {
    {.length_tag = 93, .data_tag = 89},    // SignatureLength / Signature
    {.length_tag = 90, .data_tag = 91},    // SecureDataLen / SecureData
    {.length_tag = 95, .data_tag = 96},    // RawDataLength / RawData
    {.length_tag = 212, .data_tag = 213},  // XmlDataLen / XmlData
    {.length_tag = 348, .data_tag = 349},  // EncodedHeaderLen / EncodedHeader
    {.length_tag = 350, .data_tag = 351},  // EncodedMsgLen / EncodedMsg
};

[[nodiscard]] constexpr std::uint16_t data_tag_for_length(std::uint16_t length_tag) noexcept {
    for (auto const& p : length_data_table) {
        if (p.length_tag == length_tag) {
            return p.data_tag;
        }
    }
    return 0;
}

}  // namespace detail

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

    // [2b §4.3] Construct with type-erased dict opaque pointer + helper fns.
    // The dict is borrowed from the caller; the pointer/fns alias that
    // caller-owned object. No incomplete-type issues — uses void const* + fn
    // ptrs. ([PR68-02]/[PR68-10] fix.)
    using classify_fn_t = bool (*)(void const*, std::string_view, std::uint16_t) noexcept;
    using group_member_fn_t = OffsetTable::group_member_fn_t;
    using group_delim_fn_t = OffsetTable::group_delim_fn_t;

    MessageView(frame_view const& frame, std::pmr::memory_resource* mr, void const* opaque_dict,
                classify_fn_t classify_fn, group_member_fn_t group_member_fn,
                group_delim_fn_t group_delim_fn = nullptr) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr, opaque_dict, group_member_fn, group_delim_fn},
          mr_{mr},
          opaque_dict_{opaque_dict},
          classify_fn_{classify_fn},
          group_member_fn_{group_member_fn},
          group_delim_fn_{group_delim_fn},
          unk_items_{mr} {
        // Gate B PR#176 r1 root cause #1: seed the ROOT group_context ({msg_type,
        // path=[]}) HERE, unconditionally, rather than lazily only in group<>()
        // below. Every root group_slices(N) caller (C-ABI fixpp_msg_get_group,
        // direct offsets().group_slices(), and the typed group<>() path) must
        // agree on context so the no_tag-keyed cache (offset_table.cpp
        // group_slices_) can never be poisoned by an unseeded call that runs
        // before the typed path seeds it (table_ is fully built by this point —
        // msg_type() reads via table_.find(35), valid post-construction).
        table_.set_group_context(group_context{.msg_type = msg_type()});
    }

    // FR-015 / [2b §1.2]: same as above but with caller-tunable caps.
    MessageView(frame_view const& frame, std::pmr::memory_resource* mr, OffsetTable::Config cfg,
                void const* opaque_dict, classify_fn_t classify_fn,
                group_member_fn_t group_member_fn) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr, cfg, opaque_dict, group_member_fn},
          mr_{mr},
          opaque_dict_{opaque_dict},
          classify_fn_{classify_fn},
          group_member_fn_{group_member_fn},
          unk_items_{mr} {
        // See the sibling ctor above — same root group_context seed, same
        // rationale (Gate B PR#176 r1 root cause #1).
        table_.set_group_context(group_context{.msg_type = msg_type()});
    }

    MessageView(frame_view const& frame, std::pmr::memory_resource* mr) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr},
          mr_{mr},
          unk_items_{mr} {}

    explicit MessageView(frame_view const& frame) noexcept
        requires(Mode == access_mode::Iter)
        : View{frame.bytes().data(), frame.bytes().size(), frame.token()} {
    }  // [2b §6.4] thread real pool token

    [[nodiscard]] std::string_view msg_type() const noexcept [[clang::lifetimebound]] {
        return field_string(detail::tag_msg_type);
    }
    [[nodiscard]] std::uint32_t msg_seq_num() const noexcept {
        auto b = field_bytes(detail::tag_msg_seq_num);
        return parse_bounded_u32(b);
    }

    // ---- Iter streaming, dict-free ----------------------------------------
    class field_iterator {
    public:
        struct field {
            std::uint16_t tag = 0;
            std::span<const std::byte> value;
        };
        field_iterator(std::span<const std::byte> buf, std::size_t pos) noexcept
            : buf_{buf}, pos_{pos} {
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

    private:
        void advance() noexcept;
        std::span<const std::byte> buf_;
        std::size_t pos_ = 0;
        std::size_t next_ = 0;
        field cur_{};
        bool done_ = false;
        // Length+Data carry: set when the just-yielded field was a Length
        // tag, so the next (Data) field is read by fixed length.
        std::uint16_t prev_data_tag_ = 0;
        std::uint32_t prev_data_len_ = 0;
    };

    [[nodiscard]] field_iterator begin() const noexcept [[clang::lifetimebound]] {
        return field_iterator{bytes(), 0};
    }
    [[nodiscard]] field_iterator end() const noexcept [[clang::lifetimebound]] {
        return field_iterator{bytes(), bytes().size()};
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
        auto wrapped =
            core::detail::trap_throw([span, mr]() { return fixpp::decimal_t::parse(span, mr); });
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
        // `table_` BEFORE group_slices() so group()'s membership predicate
        // calls see it; the entry_context each returned entry carries gets the
        // context PUSHED with NoTag (its own container path + own no_tag), so
        // a later nested descent from one of these entries seeds the correct
        // sub-table context (offset_table.hpp/.cpp T008).
        group_context const root_ctx{.msg_type = msg_type()};
        table_.set_group_context(root_ctx);
        entry_context ctx{};
        ctx.mr = mr_;
        ctx.opaque_dict = opaque_dict_;
        ctx.group_member_fn = group_member_fn_;
        ctx.gen = token();
        ctx.parent_cache_owner = &table_;
        ctx.group_ctx = root_ctx.pushed(NoTag);
        return group_view<GroupT>{table_.group_slices(NoTag), ctx};
    }

// [2b §4.8] Contract: unknown_fields() uses the dict pointer threaded from the
// Parser that constructed this MessageView — no caller-supplied argument.
// Walks the offset table and yields entries whose tag is not `field_valid_for`-
// known in the dict, exempting framing tags 8/9/10. Builds and caches the kv
// list in the per-message arena (mr_) on first call; idempotent.
// ([PR68-02] fix — the dict is captured at Parser construction time.)
// When dict_ptr_ is null (default/dict-free construction), no tag is classified
// as known so all non-framing tags are yielded as unknown.
[[nodiscard]] unknown_fields_view unknown_fields() const noexcept
    [[clang::lifetimebound]] requires(Mode == access_mode::Index) {
        if (unk_items_built_) {
            return unknown_fields_view{
                std::span<unknown_fields_view::kv const>{unk_items_.data(), unk_items_.size()},
                token()};
        }
        unk_items_built_ = true;
        std::string_view const mtype = msg_type();
        auto const raw = bytes();
        // Framing tags 8/9/10 are always exempt from unknown classification.
        constexpr std::uint16_t kBeginString = 8;
        constexpr std::uint16_t kBodyLength = 9;
        constexpr std::uint16_t kCheckSum = 10;
        for (auto const& e : table_.entries()) {
            if (e.tag == kBeginString || e.tag == kBodyLength || e.tag == kCheckSum) {
                continue;  // framing — never unknown
            }
            // classify_fn_ is nullptr for dict-free views (all non-framing =
            // unknown); otherwise classify via the bound fn + opaque dict.
            bool const known =
                (classify_fn_ != nullptr) && classify_fn_(opaque_dict_, mtype, e.tag);
            if (!known) {
                unk_items_.push_back(unknown_fields_view::kv{
                    .tag = e.tag, .data = raw.data() + e.offset, .len = e.length});
            }
        }
        return unknown_fields_view{
            std::span<unknown_fields_view::kv const>{unk_items_.data(), unk_items_.size()},
            token()};
    }

    // 066-dict-backed-inbound-parse T003 (mechanism (b)): the ONE internal
    // membership-copy accessor shared by `fixpp_msg_clone` and the `reify`
    // owning handle to propagate this view's dictionary membership into an
    // OWNED, independently-lifetimed `table_view` — safe to outlive the
    // source session/Dictionary (`table_view`'s copy ctor deep-copies its
    // owned tables; table_view.hpp:185-192, spans "stable for lifetime"
    // :204,221). Re-concretizes `opaque_dict_` back to a `table_view`
    // (sound: every production dict-backed parse binds a real `table_view` —
    // data-model.md "Reify owning handle" accessor precondition). A
    // dict-free source (`opaque_dict_ == nullptr`) yields a
    // default-constructed (empty) copy, so the clone/reify correctly stays
    // dict-free — the correct degenerate case (contracts/inbound-parse.md
    // C4). Defined out-of-line below (mirrors this file's existing
    // out-of-line-in-header convention for `field_iterator::advance`).
    // gate-b/r1 FQ-1 (PR #181 round 1): NOT noexcept — the copy-construction
    // below can throw std::bad_alloc (table_view.hpp:188-189, "copy may throw
    // on allocation failure"). A noexcept here would convert that catchable
    // throw into std::terminate BEFORE either production caller's catch runs
    // (src/capi/message_write.cpp `catch (...)` -> FIXPP_ERR_CAPI_CONFIG_INVALID;
    // src/dictionary/reify.cpp `catch (std::bad_alloc const&)` -> dict_reify_oom),
    // defeating the dedicated OOM error code and violating fail-closed.
    [[nodiscard]] fixpp::dict::table_view membership_copy() const;

    // 066-dict-backed-inbound-parse T007/T008: companion predicate to
    // membership_copy() — true iff THIS view is itself dict-backed
    // (opaque_dict_ non-null). A clone/reify propagation site MUST bind its
    // re-framed MessageView dict-backed ONLY when this is true: binding a
    // non-null opaque_dict at an (empty) copy from a genuinely dict-free
    // source would flip OffsetTable::group()/consume_group_extent from the
    // positional dict-free fallback (gated on pointer NULLITY, not table
    // content — offset_table.cpp:441/:519) to the membership walk, which
    // fails closed on an empty table (err_required_field_missing) — NOT the
    // "clone/reify stays dict-free" degenerate case data-model.md / contracts/
    // inbound-parse.md C4 requires.
    [[nodiscard]] bool is_dict_backed() const noexcept { return opaque_dict_ != nullptr; }

private : [[nodiscard]] std::span<const std::byte> field_bytes(std::uint16_t tag) const noexcept {
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
    // Index-mode extras. mr_ declared BEFORE unk_items_ so it is initialised
    // first.
    std::pmr::memory_resource* mr_ = std::pmr::null_memory_resource();
    // [2b §4.3] / [2b §4.8] type-erased dict threaded from the Parser.
    // Uses void const* + function pointer to avoid requiring table_view to be
    // complete at class-template definition time. ([PR68-02] fix.)
    // nullptr classify_fn_ = dict-free path (all non-framing = unknown).
    void const* opaque_dict_ = nullptr;
    classify_fn_t classify_fn_ = nullptr;
    // 062 T007: threaded into every entry_context minted by group<>() below
    // (the dict-driven group-membership predicate a nested descent needs to
    // build a dict-aware sub-OffsetTable). Default nullptr on the dict-free
    // ctors, matching table_'s own dict-free construction.
    group_member_fn_t group_member_fn_ = nullptr;
    group_delim_fn_t group_delim_fn_ = nullptr;  // 083 T057 (C-8.1)
    // unk_items_: lazily built unknown-fields kv list in the per-message arena.
    // An Index-mode ctor overrides this default with the real arena
    // (`unk_items_{mr}`), but a default-constructed view and EVERY Iter-mode view
    // fall through to this initializer. The default backing is arena_upstream()
    // (null on release/Linux, new_delete under MSVC debug): MSVC's debug STL
    // heap-allocates a _Container_proxy for this vector AT CONSTRUCTION, and
    // null_memory_resource would throw bad_alloc there -> noexcept ctor ->
    // std::terminate (a silent Windows-debug abort). unk_items_ is only ever
    // *built* on the Index-mode unknown-fields path (which threads the real
    // arena), so behaviour is unchanged on every lane.
    mutable std::pmr::vector<unknown_fields_view::kv> unk_items_{::fixpp::detail::arena_upstream()};
    mutable bool unk_items_built_ = false;
};

// field_iterator::advance — dict-free; honours the static Length+Data table
// so a Data field carrying embedded SOH is delimited by its Length field.
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
            done_ = true;
            return;
        }
        if (!fixpp::wire::accumulate_tag_digit(tag, static_cast<unsigned char>(c))) {
            done_ = true;
            return;
        }
        ++i;
    }
    if (i >= buf_.size() || buf_[i] != EQ) {
        done_ = true;
        return;
    }
    ++i;  // over '='
    std::size_t vstart = i;

    // Capture and CONSUME the Length→Data carry up-front: it applies ONLY to the
    // field immediately following its Length tag, so clearing it here prevents a
    // non-adjacent later field inheriting a stale count (W-P2-1b).
    std::uint16_t const carry_tag = prev_data_tag_;
    std::uint32_t const carry_len = prev_data_len_;
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
            cur_ = field{static_cast<std::uint16_t>(tag), buf_.subspan(vstart, avail)};
            next_ = buf_.size();
            return;
        }
        std::size_t const end = vstart + carry_len;  // <= size, no wrap
        if (end < buf_.size() && buf_[end] != SOH) {
            // Non-SOH boundary: the declared length did not land on a field
            // boundary. No error channel in Iter mode → stop (W-P2-1a).
            done_ = true;
            return;
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

    if (std::uint16_t dt = detail::data_tag_for_length(static_cast<std::uint16_t>(tag)); dt != 0) {
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
    if (opaque_dict_ == nullptr) {
        return fixpp::dict::table_view{};
    }
    // Copy-constructs (deep-copies the owned tables, table_view.hpp:185-192)
    // — the result is self-contained and outlives the source session/
    // Dictionary/table_view.
    return *static_cast<fixpp::dict::table_view const*>(opaque_dict_);
}

// [2b §4.3] span-scan → token-bearing field_view helper (062 T004, N1). The
// one wire primitive that did not exist yet: reuses the dict-free
// field_iterator to locate `tag` within an arbitrary in-frame slice (e.g. a
// repeating-group entry's own bytes) and mints a field_view carrying the
// caller-supplied generation token via field_view_access::make — mirrors
// MessageView<Index>::get(tag) (:212-219) minus the OffsetTable. No
// sub-index, zero heap allocation; tolerates a missing final SOH (the
// underlying field_iterator::advance() already falls through end==size,
// :399/:405-406). On a miss, returns the SAME field-not-found error
// MessageView::get returns (wire_required_field_missing, via table_.find).
[[nodiscard]] inline core::expected_t<field_view> get(std::span<const std::byte> span
                                                      [[clang::lifetimebound]],
                                                      std::uint16_t tag,
                                                      detail::generation_token gen) noexcept {
    using iter_t = MessageView<access_mode::Iter>::field_iterator;
    for (iter_t it{span, 0}, end{span, span.size()}; !(it == end); ++it) {
        if ((*it).tag == tag) {
            auto const& f = *it;
            return field_view_access::make(f.value.data(), f.value.size(), gen);
        }
    }
    return err_required_field_missing<field_view>();
}

template <access_mode Mode = access_mode::Index>
class Parser {
public:
    Parser() noexcept = default;

    template <class TV>
    // dict_metadata is lvalue-constrained and only address-taken (opaque_dict_);
    // forwarding would be wrong, so missing-std-forward is a false positive here.
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward)
    explicit Parser(TV&& dict_metadata) noexcept
        // gate-b/r1 FQ-2 (PR #181 round 1): constrained to fixpp::dict::table_view
        // (not merely any lvalue-referenced duck-typed dict). membership_copy()
        // (above) unconditionally static_cast<table_view const*>(opaque_dict_) --
        // sound by construction only if every dict-backed Parser<> is built over
        // a real table_view. Census (src/+tests/+bench/): every construction site
        // already passes a table_view (tests/support/mock_dict_table.hpp is a
        // compatibility shim over table_view, not a distinct type).
        requires(std::is_lvalue_reference_v<TV &&> &&
                 std::same_as<std::remove_cvref_t<TV>, fixpp::dict::table_view>)
        : opaque_dict_{std::addressof(dict_metadata)},
          classify_fn_{[](void const* d, std::string_view mt, std::uint16_t t) noexcept -> bool {
              using dict_t = std::remove_reference_t<TV>;
              return static_cast<dict_t const*>(d)->field_valid_for(mt, t);
          }},
          group_member_fn_{
              // 063 T015: resolves via the stored `group_context` (msg_type +
              // bounded parent-no_tag path) — the context-scoped membership
              // key (data-model.md "GroupMembership", Option A). Fixes
              // Defect A: a reused NumInGroup tag (e.g. FIX44 295) now
              // resolves to the members it has in THIS message/parent-path,
              // not whichever variant the loader saw first, PROVIDED the
              // context (msg_type + full parent-no_tag path) is supplied —
              // which it always is on this call site (ctx comes from the
              // stored OffsetTable context, seeded at MessageView::group<>()
              // / build_nested_subview(), parser.hpp / offset_table.cpp).
              // `table_view`'s context accessor falls back to the legacy
              // bare-`no_tag` store on a MISS (table_view.hpp doc, amended
              // hardening invariant) — unreachable HERE because this call
              // site's ctx always matches the exact registration key.
              [](void const* d, group_context const& ctx, std::uint16_t no_tag,
                 std::uint16_t tag) noexcept -> bool {
                  using dict_t = std::remove_reference_t<TV>;
                  auto const members = static_cast<dict_t const*>(d)->group_member_tags(
                      ctx.msg_type,
                      std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
                  for (auto const member_tag : members) {
                      if (member_tag == tag) {
                          return true;
                      }
                  }
                  return false;
              }},
          // 083 T057 (C-8.1): the delimiter sibling of the membership lambda,
          // resolving through the SAME opaque_dict and the SAME context key.
          group_delim_fn_{[](void const* d, group_context const& ctx,
                             std::uint16_t no_tag) noexcept -> std::uint16_t {
              using dict_t = std::remove_reference_t<TV>;
              return static_cast<dict_t const*>(d)->group_first_field(
                  ctx.msg_type,
                  std::span<std::uint16_t const>{ctx.parent_path.data(), ctx.depth}, no_tag);
          }} {}

    template <class TV>
    explicit Parser(TV&&) noexcept
        requires(!std::is_lvalue_reference_v<TV &&>)
    = delete;

    Parser(Parser const&) = delete;
    Parser& operator=(Parser const&) = delete;
    Parser(Parser&&) = delete;
    Parser& operator=(Parser&&) = delete;

    [[nodiscard]] core::expected_t<MessageView<Mode>> parse(frame_view const& frame
                                                            [[clang::lifetimebound]],
                                                            std::pmr::memory_resource* mr) noexcept
        [[clang::lifetimebound]] {
        // Thread the opaque dict pointer + helper fns into the MessageView.
        MessageView<Mode> mv{frame, mr, opaque_dict_, classify_fn_, group_member_fn_,
                             group_delim_fn_};
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
        MessageView<Mode> mv{frame, mr, cfg, opaque_dict_, classify_fn_, group_member_fn_};
        if (auto s = mv.offsets().build_status(); !s) {
            return core::expected_t<MessageView<Mode>>{std::unexpect, s.error()};
        }
        return mv;
    }

[[nodiscard]] core::expected_t<MessageView<access_mode::Iter>> parse_iter(
    frame_view const& frame [[clang::lifetimebound]]) noexcept
    [[clang::lifetimebound]] requires(Mode == access_mode::Iter) {
        return MessageView<access_mode::Iter>{frame};
    }

private : void const* opaque_dict_ = nullptr;
    bool (*classify_fn_)(void const*, std::string_view, std::uint16_t) noexcept = nullptr;
    OffsetTable::group_member_fn_t group_member_fn_ = nullptr;
    OffsetTable::group_delim_fn_t group_delim_fn_ = nullptr;
};

}  // namespace fixpp::wire
