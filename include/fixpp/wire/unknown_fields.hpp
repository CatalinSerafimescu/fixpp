#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// include/fixpp/wire/unknown_fields.hpp
// [2b §4.8] unknown_fields_view : View. Yields ONLY dictionary-MISSING tags
// (dictionary-known-but-invalid-for-MsgType tags are NOT here — those raise
// wire_unexpected_tag via validator rule 5). No vector materialization: the
// view walks the parser's offset table in document order, so round-trip
// preserves original byte order.
// Authority: .specify/2b-wire.md v0.2; shape oracle contracts/unknown_fields.hpp.
//
// ⚠️ As built since W1 (`.specify/688-707-559-wire-headroom-token.md` D-688-1, fixpp#688):
// the view is the filtered view [2b §4.8] specifies, replacing a list the MessageView
// materialised in its parse resource. It stores nothing: it holds the remaining offset-table
// entries, the frame's base, the dictionary's classify hook and the MsgType, and classifies as
// it goes, so no resource state can change what it yields. "Unknown" is a tag the dictionary
// does not define (classify_fn() answers table_view::tag_defined, ruling R-W1-3); framing tags
// 8, 9 and 10 are never unknown; a dict-free view (no classify hook) yields every other tag.
//
// The iterator contract follows from that shape:
//   - operator* returns kv by value: the iterator holds its scan state, not a kv, so a
//     reference into it would dangle across ++;
//   - begin() and end() carry no [[clang::lifetimebound]]: an iterator does not point into the
//     view, so one taken from a temporary view stays valid while the MessageView's parse storage
//     and the dictionary live;
//   - operator* and operator++ read the frame and the offset table, so they check the
//     generation token ([2b §6.4]); begin(), end() and empty() read nothing after construction.
// The dictionary the view classifies against must outlive every iteration, not only the
// unknown_fields() call.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "dict_hooks.hpp"
#include "offset_table.hpp"
#include "view.hpp"

#ifndef NDEBUG
#include <cstdlib>
#endif

namespace fixpp::wire {

enum class access_mode : std::uint8_t;
template <access_mode Mode>
class MessageView;

class unknown_fields_view : public View {
public:
    struct kv {
        std::uint16_t tag = 0;
        std::byte const* data = nullptr;
        std::size_t len = 0;
    };

private:
    using entry = OffsetTable::entry;

    // What a scan needs besides its position, shared by the view and its iterators.
    struct scan_state {
        entry const* end = nullptr;
        std::byte const* base = nullptr;
        dict_hooks::classify_fn_t classify = nullptr;
        void const* dict = nullptr;
        std::string_view msg_type{};
    };

    // The first entry at or after `p` that is unknown, or `s.end`.
    [[nodiscard]] static entry const* next_unknown(entry const* p, scan_state const& s) noexcept {
        constexpr std::uint16_t kBeginString = 8;
        constexpr std::uint16_t kBodyLength = 9;
        constexpr std::uint16_t kCheckSum = 10;
        for (; p != s.end; ++p) {
            if (p->tag == kBeginString || p->tag == kBodyLength || p->tag == kCheckSum) {
                continue;  // framing — never unknown
            }
            if (s.classify != nullptr && s.classify(s.dict, s.msg_type, p->tag)) {
                continue;  // defined by the dictionary
            }
            break;
        }
        return p;
    }

public:
    constexpr unknown_fields_view() noexcept = default;

    class iterator {
    public:
        [[nodiscard]] kv operator*() const noexcept {
            check_alive();
            return kv{.tag = cur_->tag, .data = s_.base + cur_->offset, .len = cur_->length};
        }
        iterator& operator++() noexcept {
            check_alive();
            cur_ = next_unknown(cur_ + 1, s_);
            return *this;
        }
        [[nodiscard]] bool operator==(iterator const& o) const noexcept { return cur_ == o.cur_; }

    private:
        friend class unknown_fields_view;
        iterator(scan_state const& s, entry const* cur, detail::generation_token gen) noexcept
            : s_{s},
              cur_{cur}
#ifndef NDEBUG
              ,
              gen_{gen}
#endif
        {
#ifdef NDEBUG
            (void)gen;
#endif
        }

        // [2b §6.4]: the same trap as View::check_alive(), on the token the view was built with.
        void check_alive() const noexcept {
#ifndef NDEBUG
            std::uint16_t const* slot = detail::pool_generation_slot(gen_.pool_id);
            if (slot != nullptr && *slot != gen_.gen) {
                std::abort();  // use-after-buffer-reuse — debug trap
            }
#endif
        }

        scan_state s_{};
        entry const* cur_ = nullptr;
#ifndef NDEBUG
        detail::generation_token gen_{};
#endif
    };

    [[nodiscard]] iterator begin() const noexcept { return iterator{s_, first_, token()}; }
    [[nodiscard]] iterator end() const noexcept { return iterator{s_, s_.end, token()}; }
    // Contract-mandated API (shape oracle contracts/unknown_fields.hpp):
    // "no unknown fields", deliberately distinct from View::empty()'s
    // "flyweight has no bytes". The shadow is intentional, not a slicing
    // hazard (value type, no virtual dispatch). [see 004-wire-codec-verify §run 2]
    // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
    [[nodiscard]] bool empty() const noexcept { return first_ == s_.end; }

private:
    template <access_mode>
    friend class MessageView;

    // Built by MessageView<Index>::unknown_fields() alone. The first unknown entry is found here,
    // so begin() and empty() are O(1); View's base is that entry's value bytes, or none.
    unknown_fields_view(scan_state const& s, entry const* first,
                        detail::generation_token gen) noexcept
        : View{first == s.end ? nullptr : s.base + first->offset,
               first == s.end ? 0 : first->length, gen},
          s_{s},
          first_{first} {}

    static unknown_fields_view scan(entry const* begin, entry const* end, std::byte const* base,
                                    dict_hooks::classify_fn_t classify, void const* dict,
                                    std::string_view msg_type,
                                    detail::generation_token gen) noexcept {
        scan_state const s{
            .end = end, .base = base, .classify = classify, .dict = dict, .msg_type = msg_type};
        return unknown_fields_view{s, next_unknown(begin, s), gen};
    }

    scan_state s_{};
    entry const* first_ = nullptr;
};

}  // namespace fixpp::wire
