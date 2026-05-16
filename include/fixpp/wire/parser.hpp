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

#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

#include <fixpp/core/decimal_alias.hpp>    // fixpp::decimal_t (2a/001 trait)
#include <fixpp/core/decimal_helpers.hpp>  // core::detail::trap_throw (C1)
#include <fixpp/core/error.hpp>

#include "field_view.hpp"
#include "framer.hpp"
#include "group_view.hpp"
#include "offset_table.hpp"
#include "unknown_fields.hpp"
#include "view.hpp"

namespace fixpp::dict {
class table_view;  // value type, owned by 2c; only forward-declared here.
}  // namespace fixpp::dict

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
    {93, 89},   // SignatureLength / Signature
    {90, 91},   // SecureDataLen / SecureData
    {95, 96},   // RawDataLength / RawData
    {212, 213}, // XmlDataLen / XmlData
    {348, 349}, // EncodedHeaderLen / EncodedHeader
    {350, 351}, // EncodedMsgLen / EncodedMsg
};

[[nodiscard]] constexpr std::uint16_t
data_tag_for_length(std::uint16_t length_tag) noexcept {
    for (auto const& p : length_data_table) {
        if (p.length_tag == length_tag) {
            return p.data_tag;
        }
    }
    return 0;
}

[[nodiscard]] inline std::uint32_t
parse_u32(std::span<const std::byte> v) noexcept {
    std::uint32_t out = 0;
    for (auto b : v) {
        auto c = static_cast<unsigned char>(b);
        if (c < '0' || c > '9') {
            break;
        }
        out = out * 10U + static_cast<std::uint32_t>(c - '0');
    }
    return out;
}

}  // namespace detail

template <access_mode Mode>
class MessageView : public View {
public:
    constexpr MessageView() noexcept = default;

    MessageView(frame_view const& frame, std::pmr::memory_resource* mr) noexcept
        requires (Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(), {}},
          table_{frame, mr} {}

    explicit MessageView(frame_view const& frame) noexcept
        requires (Mode == access_mode::Iter)
        : View{frame.bytes().data(), frame.bytes().size(), {}} {}

    [[nodiscard]] std::string_view
    msg_type() const noexcept [[clang::lifetimebound]] {
        return field_string(detail::tag_msg_type);
    }
    [[nodiscard]] std::uint32_t msg_seq_num() const noexcept {
        auto b = field_bytes(detail::tag_msg_seq_num);
        return detail::parse_u32(b);
    }

    // ---- Iter streaming, dict-free ----------------------------------------
    class field_iterator {
    public:
        struct field {
            std::uint16_t tag = 0;
            std::span<const std::byte> value{};
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
        [[nodiscard]] bool
        operator==(field_iterator const& o) const noexcept {
            return pos_ == o.pos_ && done_ == o.done_;
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
    [[nodiscard]] OffsetTable const&
    offsets() const noexcept [[clang::lifetimebound]]
        requires (Mode == access_mode::Index) {
        return table_;
    }

    template <std::uint16_t Tag>
    [[nodiscard]] core::expected_t<field_view> get() const noexcept
        [[clang::lifetimebound]] requires (Mode == access_mode::Index) {
        return get(Tag);
    }

    [[nodiscard]] core::expected_t<field_view>
    get(std::uint16_t tag) const noexcept [[clang::lifetimebound]]
        requires (Mode == access_mode::Index) {
        auto e = table_.find(tag);
        if (!e) {
            return core::expected_t<field_view>{std::unexpect, e.error()};
        }
        return field_view_access::make(bytes().data() + e->offset,
                                       e->length, token());
    }

    // 004-authored 001 wire FLOAT-field accessor leg (D-17, FR-006 /
    // [2b §7.1]). The wire layer performs NO decoding: it hands the field's
    // raw bytes across the 2a trait-decode boundary to
    // fixpp::decimal_t::parse(span, mr). (C1) the trait call is fenced by
    // core::detail::trap_throw so a throwing custom FIXPP_DECIMAL_T trait
    // cannot escape the noexcept parse->fromApp window (FR-013, [arch §5.3]).
    [[nodiscard]] core::expected_t<fixpp::decimal_t>
    get_decimal(std::uint16_t tag,
                std::pmr::memory_resource* mr) const noexcept
        [[clang::lifetimebound]] requires (Mode == access_mode::Index) {
        auto fv = get(tag);
        if (!fv) {
            return core::expected_t<fixpp::decimal_t>{
                std::unexpect, fv.error()};
        }
        auto span = fv->bytes();
        // trap_throw wraps the (possibly throwing) trait; result is
        // expected<expected<decimal_t>> — flatten it.
        auto wrapped = core::detail::trap_throw(
            [span, mr]() { return fixpp::decimal_t::parse(span, mr); });
        if (!wrapped) {
            return core::expected_t<fixpp::decimal_t>{
                std::unexpect, wrapped.error()};
        }
        return *wrapped;
    }

    template <std::uint16_t NoTag, class GroupT>
    [[nodiscard]] group_view<GroupT> group() const noexcept
        [[clang::lifetimebound]] requires (Mode == access_mode::Index) {
        // Delimiter-aware instance slicing uses the group's first field
        // (the entry immediately after the count). Each reappearance of
        // that delimiter tag starts a new occurrence (document order; the
        // dictionary-driven nested-group refinement is layered by 2c's
        // GroupT). Slices are owned by this view's frame (zero-copy).
        using slice = typename group_view<GroupT>::slice;
        static thread_local std::vector<slice> slices;  // borrowed below
        slices.clear();
        auto gi = table_.group(NoTag);
        if (gi) {
            auto ents = table_.entries();
            std::size_t first = gi->first_entry();
            if (first < ents.size()) {
                std::uint16_t delim = ents[first].tag;
                std::size_t inst_start = first;
                for (std::size_t k = first; k <= ents.size(); ++k) {
                    bool boundary = (k == ents.size())
                                    || (k > first && ents[k].tag == delim);
                    if (boundary) {
                        std::byte const* d =
                            bytes().data() + ents[inst_start].offset;
                        std::size_t len = (ents[k - 1].offset
                                           + ents[k - 1].length)
                                          - ents[inst_start].offset;
                        slices.push_back(slice{d, len});
                        inst_start = k;
                    }
                }
            }
        }
        return group_view<GroupT>{
            std::span<slice const>{slices.data(), slices.size()}, token()};
    }

    [[nodiscard]] unknown_fields_view unknown_fields() const noexcept
        [[clang::lifetimebound]] requires (Mode == access_mode::Index) {
        // Without the 2c dictionary bound here, no tag is classified as
        // dictionary-missing, so this is empty by construction. The
        // dictionary-aware split (missing vs known-invalid) is exercised
        // through the validator seam (US4) which holds table_view by value.
        return unknown_fields_view{};
    }

private:
    [[nodiscard]] std::span<const std::byte>
    field_bytes(std::uint16_t tag) const noexcept {
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
    [[nodiscard]] std::string_view
    field_string(std::uint16_t tag) const noexcept {
        auto b = field_bytes(tag);
        return {reinterpret_cast<char const*>(b.data()), b.size()};
    }

    struct empty_t {};
    [[no_unique_address]] std::conditional_t<
        Mode == access_mode::Index, OffsetTable, empty_t> table_{};
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
        tag = tag * 10U + static_cast<std::uint32_t>(c - '0');
        ++i;
    }
    if (i >= buf_.size() || buf_[i] != EQ) {
        done_ = true;
        return;
    }
    ++i;  // over '='
    std::size_t vstart = i;

    // Length+Data: if the PREVIOUS field was a Length tag, this Data field's
    // length is fixed by it (value may contain SOH).
    if (prev_data_tag_ != 0
        && static_cast<std::uint16_t>(tag) == prev_data_tag_) {
        std::size_t end = vstart + prev_data_len_;
        if (end > buf_.size()) {
            end = buf_.size();
        }
        cur_ = field{static_cast<std::uint16_t>(tag),
                     buf_.subspan(vstart, end - vstart)};
        next_ = (end < buf_.size()) ? end + 1 : end;  // skip trailing SOH
        prev_data_tag_ = 0;
        prev_data_len_ = 0;
        return;
    }

    while (i < buf_.size() && buf_[i] != SOH) {
        ++i;
    }
    cur_ = field{static_cast<std::uint16_t>(tag),
                 buf_.subspan(vstart, i - vstart)};
    next_ = (i < buf_.size()) ? i + 1 : i;

    if (std::uint16_t dt = detail::data_tag_for_length(
            static_cast<std::uint16_t>(tag));
        dt != 0) {
        prev_data_tag_ = dt;
        prev_data_len_ = detail::parse_u32(cur_.value);
    }
}

template <access_mode Mode = access_mode::Index>
class Parser {
public:
    // dict_metadata is a value-typed metadata contract owned by 2c (only
    // forward-declared in this header). The parse path here is
    // dictionary-free (Index/Iter decode no fields), so it is not retained.
    // The ctor is templated on the metadata type purely so its completeness
    // is deferred to instantiation (a non-dependent by-value incomplete
    // param would be ill-formed at template-definition time); TV deduces to
    // fixpp::dict::table_view at every call site, preserving the [2b §4.3]
    // by-value surface.
    template <class TV = fixpp::dict::table_view>
    explicit Parser(TV /*dict_metadata*/) noexcept {}

    [[nodiscard]] core::expected_t<MessageView<Mode>>
    parse(frame_view const& frame [[clang::lifetimebound]],
          std::pmr::memory_resource* mr) noexcept [[clang::lifetimebound]] {
        MessageView<Mode> mv{frame, mr};
        if constexpr (Mode == access_mode::Index) {
            if (auto s = mv.offsets().build_status(); !s) {
                return core::expected_t<MessageView<Mode>>{
                    std::unexpect, s.error()};
            }
        }
        return mv;
    }

    [[nodiscard]] core::expected_t<MessageView<access_mode::Iter>>
    parse_iter(frame_view const& frame [[clang::lifetimebound]]) noexcept
        [[clang::lifetimebound]] requires (Mode == access_mode::Iter) {
        return MessageView<access_mode::Iter>{frame};
    }
};

}  // namespace fixpp::wire
