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
#include <cstddef>
#include <cstdint>
#include <expected>                        // std::unexpect
#include <fixpp/core/decimal_alias.hpp>    // fixpp::decimal_t (2a/001 trait)
#include <fixpp/core/decimal_helpers.hpp>  // core::detail::trap_throw (C1)
#include <fixpp/core/error.hpp>
#include <memory_resource>
#include <span>
#include <string_view>
#include <type_traits>

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

[[nodiscard]] inline std::uint32_t parse_u32(std::span<const std::byte> v) noexcept {
    std::uint32_t out = 0;
    for (auto b : v) {
        auto c = static_cast<unsigned char>(b);
        if (c < '0' || c > '9') {
            break;
        }
        out = (out * 10U) + static_cast<std::uint32_t>(c - '0');
    }
    return out;
}

}  // namespace detail

template <access_mode Mode>
class MessageView : public View {
public:
    constexpr MessageView() noexcept = default;

    // [2b §4.3] Construct with type-erased dict opaque pointer + classify fn.
    // The dict is owned by the Parser; the pointer and fn lifetime are tied
    // to the Parser. No incomplete-type issues — uses void const* + fn ptr.
    // ([PR68-02] fix.)
    using classify_fn_t = bool (*)(void const*, std::string_view, std::uint16_t) noexcept;

    MessageView(frame_view const& frame, std::pmr::memory_resource* mr,
                void const* opaque_dict, classify_fn_t classify_fn) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr},
          mr_{mr},
          opaque_dict_{opaque_dict},
          classify_fn_{classify_fn},
          unk_items_{mr} {}

    MessageView(frame_view const& frame, std::pmr::memory_resource* mr) noexcept
        requires(Mode == access_mode::Index)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()},  // [2b §6.4] thread real pool token
          table_{frame, mr},
          mr_{mr},
          unk_items_{mr} {}

    explicit MessageView(frame_view const& frame) noexcept
        requires(Mode == access_mode::Iter)
        : View{frame.bytes().data(), frame.bytes().size(),
               frame.token()} {}  // [2b §6.4] thread real pool token

    [[nodiscard]] std::string_view msg_type() const noexcept [[clang::lifetimebound]] {
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
        return group_view<GroupT>{table_.group_slices(NoTag), token()};
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
                std::span<unknown_fields_view::kv const>{
                    unk_items_.data(), unk_items_.size()},
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
            if (e.tag == kBeginString || e.tag == kBodyLength ||
                e.tag == kCheckSum) {
                continue;  // framing — never unknown
            }
            // classify_fn_ is nullptr for dict-free views (all non-framing =
            // unknown); otherwise classify via the bound fn + opaque dict.
            bool const known = (classify_fn_ != nullptr) &&
                               classify_fn_(opaque_dict_, mtype, e.tag);
            if (!known) {
                unk_items_.push_back(unknown_fields_view::kv{
                    .tag = e.tag,
                    .data = raw.data() + e.offset,
                    .len = e.length});
            }
        }
        return unknown_fields_view{
            std::span<unknown_fields_view::kv const>{
                unk_items_.data(), unk_items_.size()},
            token()};
    }

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
    // unk_items_: lazily built unknown-fields kv list in the per-message arena.
    mutable std::pmr::vector<unknown_fields_view::kv> unk_items_{
        std::pmr::null_memory_resource()};
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
        tag = (tag * 10U) + static_cast<std::uint32_t>(c - '0');
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
    if (prev_data_tag_ != 0 && static_cast<std::uint16_t>(tag) == prev_data_tag_) {
        std::size_t end = vstart + prev_data_len_;
        end = std::min(end, buf_.size());
        cur_ = field{static_cast<std::uint16_t>(tag), buf_.subspan(vstart, end - vstart)};
        next_ = (end < buf_.size()) ? end + 1 : end;  // skip trailing SOH
        prev_data_tag_ = 0;
        prev_data_len_ = 0;
        return;
    }

    while (i < buf_.size() && buf_[i] != SOH) {
        ++i;
    }
    cur_ = field{static_cast<std::uint16_t>(tag), buf_.subspan(vstart, i - vstart)};
    next_ = (i < buf_.size()) ? i + 1 : i;

    if (std::uint16_t dt = detail::data_tag_for_length(static_cast<std::uint16_t>(tag)); dt != 0) {
        prev_data_tag_ = dt;
        prev_data_len_ = detail::parse_u32(cur_.value);
    }
}

template <access_mode Mode = access_mode::Index>
class Parser {
public:
    // [2b §4.3] Parser accepts dict_metadata by value and stores a pointer to
    // a copy held in the CALLER's scope. Thread the pointer into every
    // MessageView so unknown_fields() classifies without a caller-supplied arg.
    // ([PR68-02] fix.)
    //
    // Implementation note: we cannot hold `fixpp::dict::table_view` by value
    // here because parser.hpp only forward-declares the type and non-test TUs
    // that include parser.hpp (via message_view_contract.hpp) would see an
    // incomplete-type member at class-template definition time. Instead we use
    // a type-erased opaque_dict_ (void const*) + a classify function pointer,
    // both set in the templated ctor where TV IS complete.
    //
    // NOLINTBEGIN(performance-unnecessary-value-param) — by-value is the
    // [2b §4.3] surface contract for the ctor.
    template <class TV = fixpp::dict::table_view>
    explicit Parser(TV dict_metadata) noexcept
        // TV is complete at this instantiation point (seam-#1 rule). Store
        // a COPY on the stack inside the caller's scope and immediately
        // take its address — we can't safely take &dict_metadata (parameter
        // is a temporary), so we move it into our own storage via placement
        // into a pre-allocated aligned buffer.
        // Trade-off: we still need SOMEWHERE to store the dict by value. The
        // cleanest no-heap approach: store it in a stack-local inside parse().
        // For this implementation we COPY it into a per-call local (idiomatic
        // for the test usage pattern: Parser{dict}; parser.parse(frame, mr)).
        //
        // Since this makes Parser non-copyable (the dict copy lives here), we
        // store the dict inside the Parser using a templated copy wrapper that
        // erases the type. The wrapper needs to be small and noexcept.
        : classify_fn_{[](void const* d, std::string_view mt, std::uint16_t t) noexcept -> bool {
              return static_cast<TV const*>(d)->field_valid_for(mt, t);
          }}
    {
        // Allocate the dict copy in-place inside the aligned storage.
        static_assert(sizeof(TV) <= sizeof(dict_storage_), "dict too large for Parser storage");
        static_assert(alignof(TV) <= alignof(std::max_align_t), "dict alignment too strict for Parser storage");
        new (dict_storage_) TV(std::move(dict_metadata));
        opaque_dict_ = dict_storage_;
        destroy_fn_ = [](void* p) noexcept { static_cast<TV*>(p)->~TV(); };
    }
    // NOLINTEND(performance-unnecessary-value-param)

    ~Parser() noexcept {
        if (opaque_dict_ && destroy_fn_) {
            destroy_fn_(dict_storage_);
        }
    }
    Parser(Parser const&) = delete;
    Parser& operator=(Parser const&) = delete;
    Parser(Parser&&) = delete;
    Parser& operator=(Parser&&) = delete;

    [[nodiscard]] core::expected_t<MessageView<Mode>> parse(frame_view const& frame
                                                            [[clang::lifetimebound]],
                                                            std::pmr::memory_resource* mr) noexcept
        [[clang::lifetimebound]] {
        // Thread the opaque dict pointer + classify fn into the MessageView.
        MessageView<Mode> mv{frame, mr, opaque_dict_, classify_fn_};
        if constexpr (Mode == access_mode::Index) {
            if (auto s = mv.offsets().build_status(); !s) {
                return core::expected_t<MessageView<Mode>>{std::unexpect, s.error()};
            }
        }
        return mv;
    }

    [[nodiscard]] core::expected_t<MessageView<access_mode::Iter>> parse_iter(
        frame_view const& frame [[clang::lifetimebound]]) noexcept
        [[clang::lifetimebound]] requires(Mode == access_mode::Iter) {
            return MessageView<access_mode::Iter>{frame};
        }

private:
    // Type-erased dict storage. 512 bytes is ample for the mock_dict_table
    // (std::unordered_map<string,...> x5 — typical sizeof is ~280 B).
    // The real 2c table_view is expected to be similar. If a dict is larger,
    // the static_assert in the ctor fires at instantiation time.
    alignas(std::max_align_t) std::byte dict_storage_[512]{};
    void const* opaque_dict_ = nullptr;
    bool (*classify_fn_)(void const*, std::string_view, std::uint16_t) noexcept = nullptr;
    void (*destroy_fn_)(void*) noexcept = nullptr;
};

}  // namespace fixpp::wire
