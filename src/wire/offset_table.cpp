// SPDX-License-Identifier: AGPL-3.0-or-later
// src/wire/offset_table.cpp — [2b §6.2] OffsetTable eager build + open-address
// robin-hood overlay + lazy group sub-index. All storage from the captured
// per-message memory_resource; DoS caps enforced with bounded memory.

#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/wire/errors.hpp>  // wire::err_* / fail<T> (module error vocab)
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <memory_resource>
#include <new>
#include <span>

namespace fixpp::wire {

namespace {

constexpr std::byte SOH{0x01};
constexpr std::byte EQ{static_cast<std::byte>('=')};

// FNV-1a-ish mix for a 16-bit tag — cheap, good enough for the overlay.
constexpr std::uint32_t mix(std::uint16_t tag) noexcept {
    std::uint32_t h = 2166136261U;
    h = (h ^ (tag & 0xFFU)) * 16777619U;
    h = (h ^ ((static_cast<std::uint32_t>(tag) >> 8U) & 0xFFU)) * 16777619U;
    return h;
}

}  // namespace

std::size_t OffsetTable::overlay_cap_for(std::size_t n) noexcept {
    std::size_t want = ((n * 5U) / 4U) + 1U;  // 1.25 * n
    std::size_t cap = 8U;
    while (cap < want) {
        cap <<= 1U;
    }
    return cap;
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr) noexcept
    : entries_(mr), overlay_(mr) {
    // A noexcept ctor must NOT let a throwing `mr` (bad_alloc) escape — that
    // would std::terminate (004 T059 / Codex adversarial review: the reify
    // lazy view() rebuild made first-field-access an OOM kill-switch). On
    // allocation failure we degrade EXACTLY like the DoS-cap path below:
    // empty table, status_ = out_of_memory, find()/get<>() → field-absent.
    try {
        auto buf = frame.bytes();
        std::size_t i = 0;
        std::size_t const n = buf.size();

        while (i < n) {
            std::size_t const tag_start = i;
            std::uint32_t tag = 0;
            while (i < n && buf[i] != EQ && buf[i] != SOH) {
                auto c = static_cast<unsigned char>(buf[i]);
                if (c < '0' || c > '9') {
                    status_ = err_invalid_field_format();
                    entries_.clear();
                    return;
                }
                tag = (tag * 10U) + static_cast<std::uint32_t>(c - '0');
                ++i;
            }
            if (i >= n || buf[i] != EQ || i == tag_start) {
                status_ = err_invalid_field_format();
                entries_.clear();
                return;
            }
            if (tag > 0xFFFFU) {
                status_ = err_tag_out_of_range();
                entries_.clear();
                return;
            }
            ++i;  // step over '='
            std::size_t const val_start = i;
            while (i < n && buf[i] != SOH) {
                ++i;
            }
            std::size_t const val_len = i - val_start;
            if (i < n) {
                ++i;  // step over SOH
            }

            if (entries_.size() >= default_max_offset_entries) {
                status_ = err_offset_table_full();
                entries_.clear();
                return;
            }
            entries_.push_back(entry{.offset = static_cast<std::uint32_t>(val_start),
                                     .length = static_cast<std::uint32_t>(val_len),
                                     .tag = static_cast<std::uint16_t>(tag),
                                     .group_index_link = 0U});
        }

        // Build the robin-hood overlay over first occurrences.
        std::size_t const cap = overlay_cap_for(entries_.size());
        overlay_.assign(cap, 0U);
        auto const mask = static_cast<std::uint32_t>(cap - 1U);
        for (std::size_t e = 0; e < entries_.size(); ++e) {
            std::uint16_t const tag = entries_[e].tag;
            std::uint32_t slot = mix(tag) & mask;
            bool seen = false;
            while (overlay_[slot] != 0U) {
                if (entries_[overlay_[slot] - 1U].tag == tag) {
                    seen = true;  // keep FIRST occurrence
                    break;
                }
                slot = (slot + 1U) & mask;
            }
            if (!seen) {
                overlay_[slot] = static_cast<std::uint32_t>(e) + 1U;
            }
        }
    } catch (std::bad_alloc const&) {
        entries_.clear();
        overlay_.clear();
        status_ = fail(core::error::out_of_memory);
    }
}

core::expected_t<OffsetTable::entry> OffsetTable::find(std::uint16_t tag) const noexcept {
    if (!status_) {
        return fail<entry>(status_.error());
    }
    if (overlay_.empty()) {
        return err_required_field_missing<entry>();
    }
    auto const mask = static_cast<std::uint32_t>(overlay_.size() - 1U);
    std::uint32_t slot = mix(tag) & mask;
    std::size_t probes = 0;
    while (overlay_[slot] != 0U) {
        entry const& en = entries_[overlay_[slot] - 1U];
        if (en.tag == tag) {
            return en;
        }
        slot = (slot + 1U) & mask;
        if (++probes > overlay_.size()) {
            break;
        }
    }
    return err_required_field_missing<entry>();
}

core::expected_t<OffsetTable::group_index> OffsetTable::group(std::uint16_t no_tag) const noexcept {
    if (!status_) {
        return fail<group_index>(status_.error());
    }
    // Locate the count field (first occurrence of no_tag). Delimiter-aware
    // group membership is resolved by group_view<GroupT> with the
    // dictionary; here we bound and expose the contiguous entry range.
    std::size_t count_idx = entries_.size();
    for (std::size_t e = 0; e < entries_.size(); ++e) {
        if (entries_[e].tag == no_tag) {
            count_idx = e;
            break;
        }
    }
    if (count_idx == entries_.size()) {
        return err_required_field_missing<group_index>();
    }
    std::size_t const first = count_idx + 1U;
    std::size_t const avail = entries_.size() - first;
    if (avail > default_max_group_entries_per_instance) {
        return err_group_too_large<group_index>();
    }
    return group_index{no_tag, first, avail};
}

}  // namespace fixpp::wire
