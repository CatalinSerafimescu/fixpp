// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/wire/body_builder.cpp
//
// 061-typed-app-messages (061-slim) T006 — wire::body_builder implementation.
//
// Field-append + validation logic is a lift of src/session/business_messages.cpp's
// anonymous-namespace helpers (wfield/wchar/wdecimal, is_printable,
// is_clean_field_value — [RC#1: gate-b/r1] SOH-injection guard) into this
// wire-layer TU. is_valid_utc_timestamp is NOT lifted: it is a per-tag
// semantic check (only tag 60 / UTCTimestamp-typed fields need it) that
// requires knowing a tag's FIX *type*, which body_builder deliberately does
// not know (no wire->dictionary edge, data-model §1) — it stays owned by the
// exemplar builders that call this primitive (Phase 3 / T013-T014).
//
// Group-tree shape + count-precedence serialization + INV-5 grammar
// validation mirror src/capi/message_write.cpp's OutboundAccumulator
// (compute_entries_size/serialise_entries/validate_group_grammar,
// message_write.cpp:590-701), adapted to a memory_resource-free
// std::vector accumulator (data-model §1 "Buffer/allocation policy") and an
// author-supplied delimiter_tag instead of a dictionary lookup (INV-5,
// contracts/builder-shape-oracle.md C3).
//
// Anchors: specs/061-typed-app-messages/data-model.md §1;
//          specs/061-typed-app-messages/contracts/builder-shape-oracle.md C1-C4.

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fixpp/wire/body_builder.hpp>
#include <new>

namespace fixpp::wire {

namespace {

using fixpp::core::error;
using fixpp::core::expected_t;

// SOH byte used as the FIX field delimiter.
inline constexpr std::byte kSOH{0x01U};

// Fixed internal scratch cap for commit() serialization. Value-equal to the
// C-ABI kFrameCap (src/capi/message_write.cpp:106), which is file-static and
// NOT referenceable across translation units — body_builder tracks its own
// copy (data-model §1 "Buffer/allocation policy"; tasks.md T006 notes
// hoisting a single shared constant as v1.x debt).
inline constexpr std::size_t kBodyCap = 3800;

// Framing tags that a body-only builder must never emit (INV-2). Mirrors
// message_write.cpp's kFramingTags.
inline constexpr std::uint16_t kFramingTags[] = {8, 9, 34, 49, 52, 56, 10};

[[nodiscard]] bool is_framing_tag(std::uint16_t tag) noexcept {
    return std::ranges::any_of(kFramingTags, [tag](std::uint16_t t) { return t == tag; });
}

// Printable non-control ASCII (0x20..0x7E). Lift of business_messages.cpp's
// is_printable.
[[nodiscard]] bool is_printable(char c) noexcept {
    return static_cast<unsigned char>(c) >= 0x20U && static_cast<unsigned char>(c) <= 0x7EU;
}

// Every byte of a string field value must be printable non-control ASCII.
// Rejects SOH (0x01), NUL, DEL and all other control bytes — prevents SOH
// injection via string field values (INV-2). Lift of business_messages.cpp's
// is_clean_field_value ([RC#1: gate-b/r1]).
[[nodiscard]] bool is_clean_field_value(std::string_view sv) noexcept {
    return std::ranges::all_of(sv, is_printable);
}

[[nodiscard]] std::span<const std::byte> as_bytes(std::string_view sv) noexcept {
    return std::span<const std::byte>{reinterpret_cast<const std::byte*>(sv.data()), sv.size()};
}

}  // namespace

// ── group_instance out-of-line special members ───────────────────────────
// Deferred to here (after entry_node is complete) exactly as
// capi_internal.hpp/message_write.cpp defer GroupInstance's.

body_builder::group_instance::group_instance(std::pmr::memory_resource* mr) : fields(mr) {}
body_builder::group_instance::~group_instance() = default;
body_builder::group_instance::group_instance(group_instance&&) noexcept = default;
body_builder::group_instance& body_builder::group_instance::operator=(group_instance&&) noexcept =
    default;

// ── ctor ───────────────────────────────────────────────────────────────────

body_builder::body_builder(std::string_view msg_type) noexcept : msg_type_(msg_type) {}

// ── shared validated-append helpers ─────────────────────────────────────────

// Arena-touching append helpers: `into.emplace_back(mr)`/`.assign(...)` draw
// from body_builder::arena_ (null upstream) and THROW std::bad_alloc on
// exhaustion (data-model §1 "Buffer/allocation policy" / 061-slim rework).
// Every helper below is caught locally and converted to the typed
// wire_frame_too_large error, rolled back to the pre-call `into` size (a
// growth failure on the OUTER vector already leaves it unchanged per the
// standard's strong exception guarantee for emplace_back with a noexcept
// move -- entry_node's defaulted move is noexcept because every pmr::vector
// sub-object's move ctor is noexcept -- so the `into.size() > size_before`
// check only fires when a LATER allocation, e.g. value_bytes.assign(),
// throws after the new node was already appended).

// Core scalar append: emplace one entry and assign its pre-encoded value bytes,
// rolling back on arena exhaustion. The three typed wrappers below pre-validate
// (framing-tag reject + value validation, in the SAME order as before) and then
// forward here, so the emplace/assign/rollback logic lives in one place.
expected_t<void> body_builder::append_bytes_field(std::pmr::vector<entry_node>& into,
                                                  std::uint16_t tag,
                                                  std::span<const std::byte> value) noexcept {
    std::size_t size_before = into.size();
    try {
        into.emplace_back(into.get_allocator().resource());
        into.back().tag = tag;
        into.back().is_group = false;
        into.back().value_bytes.assign(value.begin(), value.end());
    } catch (const std::bad_alloc&) {
        if (into.size() > size_before) into.pop_back();
        return std::unexpected(error::wire_frame_too_large);
    }
    return {};
}

expected_t<void> body_builder::append_string_field(std::pmr::vector<entry_node>& into,
                                                   std::uint16_t tag, std::string_view v) noexcept {
    if (is_framing_tag(tag)) return std::unexpected(error::wire_field_value_out_of_range);
    if (!is_clean_field_value(v)) return std::unexpected(error::wire_field_value_out_of_range);
    return append_bytes_field(into, tag, as_bytes(v));
}

expected_t<void> body_builder::append_int_field(std::pmr::vector<entry_node>& into,
                                                std::uint16_t tag, std::int64_t v) noexcept {
    if (is_framing_tag(tag)) return std::unexpected(error::wire_field_value_out_of_range);
    char buf[24];
    auto res = std::to_chars(buf, buf + sizeof(buf), v);
    if (res.ec != std::errc{})
        return std::unexpected(
            error::wire_invalid_field_format);  // LCOV_EXCL_LINE — int64 always fits 24 chars
    const auto* bdata = reinterpret_cast<const std::byte*>(buf);
    return append_bytes_field(into, tag, {bdata, static_cast<std::size_t>(res.ptr - buf)});
}

expected_t<void> body_builder::append_decimal_field(std::pmr::vector<entry_node>& into,
                                                    std::uint16_t tag,
                                                    const fixpp::decimal_t& v) noexcept {
    if (is_framing_tag(tag)) return std::unexpected(error::wire_field_value_out_of_range);
    std::byte val_buf[64];
    auto fmt_r = v.format(std::span<std::byte>{val_buf});
    if (!fmt_r.has_value()) return std::unexpected(error::decimal_invalid_input);
    return append_bytes_field(into, tag, {val_buf, *fmt_r});
}

// ── flat fields ──────────────────────────────────────────────────────────

expected_t<void> body_builder::field(std::uint16_t tag, std::string_view v) noexcept {
    return append_string_field(entries_, tag, v);
}

expected_t<void> body_builder::field(std::uint16_t tag, char c) noexcept {
    return append_string_field(entries_, tag, std::string_view{&c, 1});
}

expected_t<void> body_builder::field(std::uint16_t tag, std::int64_t v) noexcept {
    return append_int_field(entries_, tag, v);
}

expected_t<void> body_builder::field(std::uint16_t tag, const fixpp::decimal_t& v) noexcept {
    return append_decimal_field(entries_, tag, v);
}

// ── handle resolution (reallocation-safe: index-path walk) ────────────────

body_builder::entry_node* body_builder::resolve_group(const group_handle& h) noexcept {
    entry_node* cur = &entries_[h.group_idx_[0]];
    for (std::uint8_t lvl = 1; lvl < h.levels_; ++lvl) {
        group_instance& gi = cur->instances[h.inst_idx_[lvl - 1]];
        cur = &gi.fields[h.group_idx_[lvl]];
    }
    return cur;
}

body_builder::group_instance* body_builder::resolve_instance(const entry_handle& e) noexcept {
    entry_node* g = resolve_group(e.group_);
    return &g->instances[e.instance_index_];
}

// ── group_begin / add_entry / group_end ────────────────────────────────────

expected_t<group_handle> body_builder::group_begin(std::uint16_t no_tag,
                                                   std::uint16_t delimiter_tag) noexcept {
    if (is_framing_tag(no_tag)) return std::unexpected(error::wire_field_value_out_of_range);

    std::size_t entries_before = entries_.size();
    std::size_t stack_before = open_stack_.size();
    try {
        entries_.emplace_back(entries_.get_allocator().resource());
        auto idx = static_cast<std::uint32_t>(entries_.size() - 1);
        entries_.back().tag = no_tag;
        entries_.back().is_group = true;
        entries_.back().delimiter_tag = delimiter_tag;

        group_handle h;
        h.owner_ = this;
        h.levels_ = 1;
        h.group_idx_[0] = idx;
        h.open_seq_ = next_open_seq_++;
        open_stack_.push_back(h.open_seq_);
        return h;
    } catch (const std::bad_alloc&) {
        if (open_stack_.size() > stack_before) open_stack_.pop_back();
        if (entries_.size() > entries_before) entries_.pop_back();
        return std::unexpected(error::wire_frame_too_large);
    }
}

expected_t<entry_handle> body_builder::add_entry_impl(const group_handle& g) noexcept {
    entry_node* ge = resolve_group(g);
    std::size_t before = ge->instances.size();
    try {
        ge->instances.emplace_back(ge->instances.get_allocator().resource());
        auto inst_idx = static_cast<std::uint32_t>(ge->instances.size() - 1);
        entry_handle e;
        e.group_ = g;
        e.instance_index_ = inst_idx;
        return e;
    } catch (const std::bad_alloc&) {
        if (ge->instances.size() > before) ge->instances.pop_back();
        return std::unexpected(error::wire_frame_too_large);
    }
}

expected_t<group_handle> body_builder::entry_group_begin_impl(
    const entry_handle& e, std::uint16_t no_tag, std::uint16_t delimiter_tag) noexcept {
    if (is_framing_tag(no_tag)) return std::unexpected(error::wire_field_value_out_of_range);
    if (e.group_.levels_ >= kMaxGroupNestDepth - 1)
        return std::unexpected(
            error::wire_frame_too_large);  // LCOV_EXCL_LINE — defensive bound; 061-slim
                                           // nests <= 3 levels, well under kMaxGroupNestDepth

    group_instance* inst = resolve_instance(e);
    std::size_t fields_before = inst->fields.size();
    std::size_t stack_before = open_stack_.size();
    try {
        inst->fields.emplace_back(inst->fields.get_allocator().resource());
        auto idx = static_cast<std::uint32_t>(inst->fields.size() - 1);
        inst->fields.back().tag = no_tag;
        inst->fields.back().is_group = true;
        inst->fields.back().delimiter_tag = delimiter_tag;

        group_handle h = e.group_;
        std::uint8_t old_levels = h.levels_;
        h.group_idx_[old_levels] = idx;
        h.inst_idx_[old_levels - 1] = e.instance_index_;
        h.levels_ = static_cast<std::uint8_t>(old_levels + 1);
        h.open_seq_ = next_open_seq_++;
        open_stack_.push_back(h.open_seq_);
        return h;
    } catch (const std::bad_alloc&) {
        if (open_stack_.size() > stack_before) open_stack_.pop_back();
        if (inst->fields.size() > fields_before) inst->fields.pop_back();
        return std::unexpected(error::wire_frame_too_large);
    }
}

expected_t<void> body_builder::group_end(group_handle handle) noexcept {
    if (open_stack_.empty() || open_stack_.back() != handle.open_seq_) {
        return std::unexpected(error::wire_invalid_field_format);
    }
    open_stack_.pop_back();
    return {};
}

// ── group_handle / entry_handle forwarding members ──────────────────────────

expected_t<entry_handle> group_handle::add_entry() noexcept {
    return owner_->add_entry_impl(*this);
}

expected_t<void> entry_handle::set_string(std::uint16_t tag, std::string_view v) noexcept {
    return body_builder::append_string_field(group_.owner_->resolve_instance(*this)->fields, tag,
                                             v);
}

expected_t<void> entry_handle::set_char(std::uint16_t tag, char c) noexcept {
    return set_string(tag, std::string_view{&c, 1});
}

expected_t<void> entry_handle::set_int(std::uint16_t tag, std::int64_t v) noexcept {
    return body_builder::append_int_field(group_.owner_->resolve_instance(*this)->fields, tag, v);
}

expected_t<void> entry_handle::set_decimal(std::uint16_t tag, const fixpp::decimal_t& v) noexcept {
    return body_builder::append_decimal_field(group_.owner_->resolve_instance(*this)->fields, tag,
                                              v);
}

expected_t<group_handle> entry_handle::group_begin(std::uint16_t no_tag,
                                                   std::uint16_t delimiter_tag) noexcept {
    return group_.owner_->entry_group_begin_impl(*this, no_tag, delimiter_tag);
}

// ── INV-5 grammar validation ────────────────────────────────────────────────

expected_t<void> body_builder::validate_group_grammar(
    const std::pmr::vector<entry_node>& entries) noexcept {
    for (const auto& e : entries) {
        if (!e.is_group) continue;
        for (const auto& inst : e.instances) {
            if (inst.fields.empty()) return std::unexpected(error::wire_invalid_field_format);
            if (inst.fields.front().tag != e.delimiter_tag)
                return std::unexpected(error::wire_invalid_field_format);
            if (auto r = validate_group_grammar(inst.fields); !r) return r;
        }
    }
    return {};
}

// ── size compute + serialize (count-precedence) ─────────────────────────────

std::size_t body_builder::compute_size(const std::pmr::vector<entry_node>& entries) noexcept {
    std::size_t total = 0;
    for (const auto& e : entries) {
        if (e.is_group) {
            char nb[8];
            auto nres = std::to_chars(nb, nb + sizeof(nb), e.tag);
            char cb[16];
            auto cres = std::to_chars(cb, cb + sizeof(cb), e.instances.size());
            total += static_cast<std::size_t>(nres.ptr - nb) + 1 +
                     static_cast<std::size_t>(cres.ptr - cb) + 1;
            for (const auto& inst : e.instances) total += compute_size(inst.fields);
        } else {
            char tb[8];
            auto tres = std::to_chars(tb, tb + sizeof(tb), e.tag);
            total += static_cast<std::size_t>(tres.ptr - tb) + 1 + e.value_bytes.size() + 1;
        }
    }
    return total;
}

void body_builder::serialize_entries(std::byte* buf, std::size_t& pos,
                                     const std::pmr::vector<entry_node>& entries) noexcept {
    for (const auto& e : entries) {
        char tb[8];
        auto tres = std::to_chars(tb, tb + sizeof(tb), e.tag);
        auto tag_len = static_cast<std::size_t>(tres.ptr - tb);
        for (std::size_t i = 0; i < tag_len; ++i) buf[pos++] = static_cast<std::byte>(tb[i]);
        buf[pos++] = static_cast<std::byte>('=');
        if (e.is_group) {
            char cb[16];
            auto cres = std::to_chars(cb, cb + sizeof(cb), e.instances.size());
            for (const char* p = cb; p < cres.ptr; ++p) buf[pos++] = static_cast<std::byte>(*p);
            buf[pos++] = kSOH;
            for (const auto& inst : e.instances) serialize_entries(buf, pos, inst.fields);
        } else {
            for (auto b : e.value_bytes) buf[pos++] = b;
            buf[pos++] = kSOH;
        }
    }
}

// ── commit ───────────────────────────────────────────────────────────────

expected_t<std::span<std::byte>> body_builder::commit(std::span<std::byte> out) noexcept {
    // INV-4: any group still open (LIFO stack not empty) -> typed error.
    if (!open_stack_.empty()) return std::unexpected(error::wire_invalid_field_format);

    // INV-5: every group instance is non-empty and delimiter-first.
    if (auto r = validate_group_grammar(entries_); !r) return std::unexpected(r.error());

    std::size_t total = 3 + msg_type_.size() + 1;  // "35=" + msg_type + SOH
    total += compute_size(entries_);

    // Fixed internal scratch cap (data-model §1 "Buffer/allocation policy").
    if (total > kBodyCap) return std::unexpected(error::wire_frame_too_large);
    // Caller-supplied `out` must be large enough.
    if (out.size() < total) return std::unexpected(error::wire_frame_too_large);

    // INV-4: assemble into local scratch; copy to `out` only on full success.
    // Not value-initialized: serialize_entries writes exactly [0,total) and only
    // those bytes are copied to `out`, so zeroing 3800 B here is pure waste.
    std::array<std::byte, kBodyCap> scratch;
    std::size_t pos = 0;
    scratch[pos++] = static_cast<std::byte>('3');
    scratch[pos++] = static_cast<std::byte>('5');
    scratch[pos++] = static_cast<std::byte>('=');
    for (char c : msg_type_) scratch[pos++] = static_cast<std::byte>(c);
    scratch[pos++] = kSOH;

    serialize_entries(scratch.data(), pos, entries_);

    for (std::size_t i = 0; i < total; ++i) out[i] = scratch[i];
    return out.subspan(0, total);
}

}  // namespace fixpp::wire
