// SPDX-License-Identifier: AGPL-3.0-or-later
// src/wire/offset_table.cpp — [2b §6.2] OffsetTable eager build + open-address
// robin-hood overlay + lazy group sub-index. All storage from the captured
// per-message memory_resource; DoS caps enforced with bounded memory.

#include <atomic>
#include <chrono>  // noexcept seed fallback entropy (W-P3-2)
#include <cstddef>
#include <cstdint>
#include <random>  // per-process overlay seed (W-P3-2)
#include <fixpp/core/error.hpp>
#include <fixpp/wire/errors.hpp>  // wire::err_* / fail<T> (module error vocab)
#include <fixpp/wire/framer.hpp>
#include <fixpp/wire/offset_table.hpp>
#include <fixpp/wire/tag_scan.hpp>  // accumulate_tag_digit (SC-004 / 040-inbound-tag-overflow)
#include <fixpp/wire/view.hpp>  // group_slice
#include <memory_resource>
#include <new>
#include <span>

namespace fixpp::wire {

namespace {

constexpr std::byte SOH{0x01};
constexpr std::byte EQ{static_cast<std::byte>('=')};

// Static Length+Data tag pairs ([FIX50SP2 §3]). When a Length tag is seen,
// the NEXT field (the Data tag) is read by fixed byte count, not by SOH
// delimiter, so embedded SOH bytes inside Data values are handled correctly.
// Duplicated from the same table in parser.hpp (which we cannot include here
// to avoid a circular dependency); kept in sync with the parser's copy.
struct len_data_pair_t {
    std::uint16_t length_tag;
    std::uint16_t data_tag;
};
constexpr len_data_pair_t len_data_pairs[] = {
    {.length_tag = 93, .data_tag = 89},    // SignatureLength / Signature
    {.length_tag = 90, .data_tag = 91},    // SecureDataLen / SecureData
    {.length_tag = 95, .data_tag = 96},    // RawDataLength / RawData
    {.length_tag = 212, .data_tag = 213},  // XmlDataLen / XmlData
    {.length_tag = 348, .data_tag = 349},  // EncodedHeaderLen / EncodedHeader
    {.length_tag = 350, .data_tag = 351},  // EncodedMsgLen / EncodedMsg
};

[[nodiscard]] constexpr std::uint16_t data_tag_for(std::uint16_t length_tag) noexcept {
    for (auto const& p : len_data_pairs) {
        if (p.length_tag == length_tag) {
            return p.data_tag;
        }
    }
    return 0;
}

// Given the byte offset of the value (val_start, the byte AFTER '='),
// walk backward past the '=' and the tag digits to find the byte index of
// the first tag digit (i.e., the start of the full "tag=value" field).
// Returns val_start if the buffer layout is somehow invalid (defense).
constexpr std::uint32_t field_start_from_val(std::byte const* frame_base,
                                             std::uint32_t val_start) noexcept {
    if (val_start == 0U) {
        return 0U;  // pathological: no room for '='
    }
    // val_start - 1 is the '=' byte.  Walk backward past tag digits.
    std::uint32_t p = val_start - 1U;  // points at '='
    while (p > 0U) {
        auto c = static_cast<unsigned char>(frame_base[p - 1U]);
        if (c < '0' || c > '9') {
            break;  // the byte before is NOT a digit — p-1 is the field start
        }
        --p;
    }
    // p now points at the first tag digit (or 0 if the tag starts at offset 0)
    // but we need to handle the case where p==val_start-1 (no digits walked):
    // that means val_start-1 IS already the '=' which makes no sense for a
    // valid tag=value field; just return as-is (paranoia guard).
    return p;
}

// FNV-1a-ish mix for a 16-bit tag — cheap, good enough for the overlay. The
// per-process `seed` is XORed into the FNV basis so the slot for a tag is
// unpredictable across processes, defeating the crafted-collision false-absent
// (W-P3-2). One XOR on constants the optimizer already folds — no hot-path cost.
constexpr std::uint32_t mix(std::uint16_t tag, std::uint32_t seed) noexcept {
    std::uint32_t h = 2166136261U ^ seed;
    h = (h ^ (tag & 0xFFU)) * 16777619U;
    h = (h ^ ((static_cast<std::uint32_t>(tag) >> 8U) & 0xFFU)) * 16777619U;
    return h;
}

// Compute the per-process overlay seed once. noexcept-safe: build() (and all
// four ctors) are noexcept, so a throwing std::random_device must NOT escape and
// std::terminate — fall back to a coarse entropy mix (steady_clock XOR an ASLR'd
// address, splitmix-finalised). Either way the result is unpredictable to a
// remote peer, which is all W-P3-2 needs (the reject-oracle is far too weak for
// seed recovery at 16-bit tags / whole authenticated frames per probe).
std::uint32_t compute_process_seed() noexcept {
    try {
        std::random_device rd;
        return static_cast<std::uint32_t>(rd());
    } catch (...) {
        auto ticks = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        static int anchor = 0;
        std::uint64_t x = ticks ^ static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&anchor));
        x ^= x >> 30U;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27U;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31U;
        return static_cast<std::uint32_t>(x);
    }
}

// Process-wide seed storage (magic static init once). Read into each table's
// seed_ at build() so find()'s hot path never touches the guard variable.
// Atomic so the TEST/FUZZ-ONLY set_overlay_seed_for_testing() hook can never
// race a concurrent build()'s read (Gate B PR #166 round-1 Finding 2c).
std::atomic<std::uint32_t>& overlay_seed_storage() noexcept {
    static std::atomic<std::uint32_t> seed{compute_process_seed()};
    return seed;
}

}  // namespace

namespace detail {
void set_overlay_seed_for_testing(std::uint32_t seed) noexcept {
    overlay_seed_storage().store(seed, std::memory_order_relaxed);
}
}  // namespace detail

std::size_t OffsetTable::overlay_cap_for(std::size_t n) noexcept {
    std::size_t want = ((n * 5U) / 4U) + 1U;  // 1.25 * n
    std::size_t cap = 8U;
    while (cap < want) {
        cap <<= 1U;
    }
    return cap;
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr) noexcept
    : cfg_{},

      entries_(mr),
      overlay_(mr),
      group_slices_(mr),
      group_index_(mr),
      nested_cache_(mr) {
    build(frame);
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr,
                         void const* opaque_dict, group_member_fn_t group_member_fn) noexcept
    : cfg_{},
      opaque_dict_{opaque_dict},
      group_member_fn_{group_member_fn},
      entries_(mr),
      overlay_(mr),
      group_slices_(mr),
      group_index_(mr),
      nested_cache_(mr) {
    build(frame);
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr,
                         Config cfg) noexcept
    : cfg_{cfg},

      entries_(mr),
      overlay_(mr),
      group_slices_(mr),
      group_index_(mr),
      nested_cache_(mr) {
    build(frame);
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr, Config cfg,
                         void const* opaque_dict, group_member_fn_t group_member_fn) noexcept
    : cfg_{cfg},
      opaque_dict_{opaque_dict},
      group_member_fn_{group_member_fn},
      entries_(mr),
      overlay_(mr),
      group_slices_(mr),
      group_index_(mr),
      nested_cache_(mr) {
    build(frame);
}

void OffsetTable::build(frame_view const& frame) noexcept {
    // A noexcept build must NOT let a throwing `mr` (bad_alloc) escape — that
    // would std::terminate (004 T059 / Codex adversarial review: the reify
    // lazy view() rebuild made first-field-access an OOM kill-switch). On
    // allocation failure we degrade EXACTLY like the DoS-cap path below:
    // empty table, status_ = out_of_memory, find()/get<>() → field-absent.
    try {
        auto buf = frame.bytes();
        frame_base_ = buf.data();
        seed_ = overlay_seed_storage().load(std::memory_order_relaxed);  // snapshot (W-P3-2)
        std::size_t i = 0;
        std::size_t const n = buf.size();

        // Length+Data carry: when the previous field was a Length tag, the
        // current (Data) field is read by fixed byte count rather than SOH
        // delimiter so embedded SOH inside Data values is handled correctly.
        std::uint16_t pending_data_tag = 0;
        std::uint32_t pending_data_len = 0;

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
                if (!fixpp::wire::accumulate_tag_digit(tag, c)) {
                    status_ = err_tag_out_of_range();
                    entries_.clear();
                    return;
                }
                ++i;
            }
            if (i >= n || buf[i] != EQ || i == tag_start) {
                status_ = err_invalid_field_format();
                entries_.clear();
                return;
            }
            ++i;  // step over '='
            std::size_t const val_start = i;
            std::size_t val_len = 0;

            // Capture and CONSUME the Length→Data carry up-front: it applies
            // ONLY to the field immediately following its Length tag. Clearing
            // it here means a non-adjacent later field can never inherit a stale
            // count (W-P2-1b) — the else arm below re-arms it only for a Length
            // tag's true successor.
            std::uint16_t const carry_tag = pending_data_tag;
            std::uint32_t const carry_len = pending_data_len;
            pending_data_tag = 0;
            pending_data_len = 0;

            // Length+Data: if the previous field was a Length tag naming THIS
            // Data tag, read it by fixed byte count instead of scanning for SOH.
            if (carry_tag != 0 && static_cast<std::uint16_t>(tag) == carry_tag) {
                // Bound via SUBTRACTION (val_start <= n always) so
                // val_start + carry_len can never wrap size_t on ANY width — the
                // latent 32-bit heap-OOB escalation (W-P2-1a). carry_len is
                // already saturated to the frame size by accumulate_bounded.
                if (carry_len > n - val_start) {
                    // Declared length runs past the frame end: malformed.
                    status_ = err_invalid_field_format();
                    entries_.clear();
                    return;
                }
                std::size_t const end = val_start + carry_len;  // <= n, no wrap
                // FIX requires the byte after a counted Data value to be SOH.
                // This whole-frame scanner always sees a trailing
                // `10=NNN<SOH>` checksum field after the body (Framer-validated
                // before build()), so a legitimate counted value can never
                // reach exactly `n` — `end == n` swallows the checksum tail and
                // is just as malformed as a non-SOH end byte (Gate B PR #166
                // Finding 1 / B-004-6). Index accepts ONLY `end < n &&
                // buf[end] == SOH`; reject otherwise rather than blindly skip
                // and desync into the next field (W-P2-1a).
                if (end >= n || buf[end] != SOH) {
                    status_ = err_invalid_field_format();
                    entries_.clear();
                    return;
                }
                val_len = carry_len;
                i = end + 1U;  // end < n verified above; step over the SOH
            } else {
                while (i < n && buf[i] != SOH) {
                    ++i;
                }
                val_len = i - val_start;
                if (i < n) {
                    ++i;  // step over SOH
                }
                // If this was a Length tag, record the expected data length for
                // the immediately-following Data tag. The length is bounded to
                // the frame size: a lying over-large length saturates to n (=>
                // the subtraction bound above rejects it) rather than wrapping
                // uint32 (W-P2-1c / P3-1).
                if (std::uint16_t const dt = data_tag_for(static_cast<std::uint16_t>(tag));
                    dt != 0) {
                    std::uint32_t dlen = 0;
                    auto const cap =
                        static_cast<std::uint32_t>(n > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : n);
                    for (std::size_t k = val_start; k < val_start + val_len; ++k) {
                        auto const c = static_cast<unsigned char>(buf[k]);
                        if (c < '0' || c > '9') {
                            break;
                        }
                        (void)fixpp::wire::accumulate_bounded(dlen, c, cap);
                    }
                    pending_data_tag = dt;
                    pending_data_len = dlen;
                }
            }

            if (entries_.size() >= cfg_.max_offset_entries) {
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
        // DoS bound: a frame whose distinct tags adversarially hash-collide
        // under mix() could make each insert probe O(occ), i.e. O(occ^2)
        // total within the wire_offset_table_full cap. Cap the per-insert
        // probe at a constant so build stays O(occ). On overflow the
        // occurrence is left UN-indexed (skipped, never written to an
        // occupied slot) — find() then reports that tag absent, a bounded,
        // crash-free degradation that only a crafted hostile frame can hit
        // (normal frames keep clusters far below the cap; load factor < 1).
        constexpr std::size_t kMaxBuildProbe = 128;
        for (std::size_t e = 0; e < entries_.size(); ++e) {
            std::uint16_t const tag = entries_[e].tag;
            std::uint32_t slot = mix(tag, seed_) & mask;
            bool skip_insert = false;
            std::size_t probes = 0;
            while (overlay_[slot] != 0U) {
                if (entries_[overlay_[slot] - 1U].tag == tag) {
                    skip_insert = true;  // keep FIRST occurrence
                    break;
                }
                slot = (slot + 1U) & mask;
                if (++probes >= kMaxBuildProbe) {
                    skip_insert = true;  // DoS bound: leave this occ un-indexed
                    break;
                }
            }
            if (!skip_insert) {
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
    std::uint32_t slot = mix(tag, seed_) & mask;
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
    if (first >= entries_.size()) {
        // Count field is the last entry; group is empty.
        return group_index{no_tag, first, 0U};
    }

    std::uint16_t const delim = entries_[first].tag;
    std::size_t group_end = first;
    if (opaque_dict_ != nullptr && group_member_fn_ != nullptr) {
        // Validate that `no_tag` is actually a group count field by confirming
        // the dictionary recognises `delim` (the tag immediately following the
        // count field) as a member of `no_tag`'s group.  If the dict does NOT
        // know about this membership the count field is a plain scalar (e.g.
        // SenderCompID=49) and we must return an absent result so that
        // group_slices() yields an empty span → the thunk can report
        // TYPE_MISMATCH (E-2 / CA-010-read contract).
        if (!group_member_fn_(opaque_dict_, no_tag, delim)) {
            return err_required_field_missing<group_index>();
        }
        std::size_t k = first;
        while (k < entries_.size()) {
            if (entries_[k].tag != delim) {
                break;
            }
            std::size_t const inst_start = k;
            ++k;  // consume delimiter
            while (k < entries_.size()) {
                if (entries_[k].tag == delim) {
                    break;  // next instance
                }
                if (!group_member_fn_(opaque_dict_, no_tag, entries_[k].tag)) {
                    break;
                }
                bool seen_in_instance = false;
                for (std::size_t m = inst_start + 1U; m < k; ++m) {
                    if (entries_[m].tag == entries_[k].tag) {
                        seen_in_instance = true;
                        break;
                    }
                }
                if (seen_in_instance) {
                    break;
                }
                ++k;
            }
            group_end = k;
        }
    } else {
        // Dict-free fallback kept for non-dictionary callers.
        group_end = entries_.size();
    }

    std::size_t inst_start = first;
    for (std::size_t k = first; k <= group_end; ++k) {
        bool const boundary = (k == group_end) || (k > first && entries_[k].tag == delim);
        if (!boundary) {
            continue;
        }
        std::size_t const inst_count = k - inst_start;
        if (inst_count > cfg_.max_group_entries_per_instance) {
            return err_group_too_large<group_index>();
        }
        inst_start = k;
    }
    return group_index{no_tag, first, group_end - first};
}

std::span<group_slice const> OffsetTable::group_slices(std::uint16_t no_tag) const noexcept {
    // Already materialized for this no_tag — return the stable cached span.
    for (auto const& gs : group_index_) {
        if (gs.no_tag == no_tag) {
            return {group_slices_.data() + gs.start, gs.count};
        }
    }
    try {
        // Reserve once to the entry-count upper bound (total instances across
        // all groups ≤ entry count): subsequent appends never reallocate, so
        // every previously returned span stays valid.
        if (!group_slices_reserved_) {
            group_slices_.reserve(entries_.size());
            group_slices_reserved_ = true;
        }
        auto const start = static_cast<std::uint32_t>(group_slices_.size());
        auto gi = group(no_tag);
        if (gi) {
            std::size_t const first = gi->first_entry();
            // group() now returns the bounded extent (excluding trailing
            // top-level fields). Use gi->entry_count() to derive group_end so
            // slices stay within the member-set boundary. ([PR68-09] fix.)
            std::size_t const group_end = first + gi->entry_count();
            if (first < entries_.size() && first < group_end) {
                // Each reappearance of the group's first field (the entry
                // after the count) starts a new occurrence; slice in document
                // order from one delimiter up to (but excluding) the next.
                // The loop is bounded by group_end, NOT entries_.size(), so
                // trailing top-level fields are never included in the last
                // instance slice. ([PR68-09] boundary fix.)
                std::uint16_t const delim = entries_[first].tag;
                std::size_t inst_start = first;
                for (std::size_t k = first; k <= group_end; ++k) {
                    bool const boundary =
                        (k == group_end) || (k > first && entries_[k].tag == delim);
                    if (boundary) {
                        // RC#2 fix: slice must begin at the delimiter's "tag="
                        // prefix, NOT at its value. Walk back from val_start to
                        // find the first digit of the tag ([2b §4.7]).
                        std::uint32_t const fs =
                            field_start_from_val(frame_base_, entries_[inst_start].offset);
                        std::byte const* d = frame_base_ + fs;
                        // End = one past the last value byte of the last field
                        // in this instance (which terminates before the trailing
                        // SOH — length is exclusive of SOH by contract).
                        std::uint32_t const end_off =
                            entries_[k - 1U].offset + entries_[k - 1U].length;
                        std::size_t const len =
                            (end_off > fs) ? static_cast<std::size_t>(end_off - fs) : 0U;
                        group_slices_.push_back(group_slice{.data = d, .len = len});
                        inst_start = k;
                    }
                }
            }
        }
        auto const count = static_cast<std::uint32_t>(group_slices_.size()) - start;
        group_index_.push_back(group_span{.no_tag = no_tag, .start = start, .count = count});
        return {group_slices_.data() + start, count};
    } catch (std::bad_alloc const&) {
        return {};  // degrade to "no instances", never throw (noexcept)
    }
}

// 062 T005: dict-aware sub-view-over-slice builder (see offset_table.hpp for
// the ownership/lifetime/RC1 contract). Placement-constructs into `mr`,
// mirroring the established `mr->allocate(size, align)` + placement-new
// arena pattern (include/fixpp/core/sync/async_mutex.hpp:1160-1164).
OffsetTable* OffsetTable::build_nested_subview(std::byte const* data, std::size_t len,
                                               std::pmr::memory_resource* mr,
                                               void const* opaque_dict,
                                               group_member_fn_t group_member_fn,
                                               detail::generation_token gen) noexcept {
    try {
        // RC1: slice-scoped `len+1` — the terminal SOH is provably already
        // present in the parent frame buffer at `data+len` (the slice is
        // interior to a well-formed, checksum-terminated frame in which
        // every field is SOH-terminated). This widens ONLY this build's
        // input span, not the shared `group_slice.len` — the whole-frame
        // `build()` guard above (`end >= n || buf[end] != SOH`) is untouched
        // and passes because the widened span's last byte IS that SOH.
        frame_view const fv = frame_view_slice_access::make(data, len + 1U, gen);
        void* mem = mr->allocate(sizeof(OffsetTable), alignof(OffsetTable));
        // Dict-aware ctor is MANDATORY on the nested-descent path (INV-G7);
        // never the dict-free fallback.
        return ::new (mem) OffsetTable(fv, mr, opaque_dict, group_member_fn);
    } catch (std::bad_alloc const&) {
        return nullptr;  // degrade: caller serves absent rather than throw
    }
}

// 062 T006: single flat nested-subview cache (see offset_table.hpp for the
// full keying/ownership contract — ROOT-owned, keyed by
// `(slice_data, nested_no_tag)`, dedupes the sub-table build across distinct
// no_tags on the same slice).
std::span<group_slice const> OffsetTable::nested_group_slices(
    std::byte const* slice_data, std::size_t slice_len, std::uint16_t nested_no_tag,
    void const* opaque_dict, group_member_fn_t group_member_fn,
    detail::generation_token gen) const noexcept {
    if (slice_data == nullptr) {
        return {};
    }
    // Single pass over the flat cache:
    //  - exact (slice, no_tag) hit → serve immediately (build-once per pair);
    //  - otherwise remember the FIRST row for this slice so a second distinct
    //    no_tag on the SAME slice reuses its already-built sub-OffsetTable (one
    //    sub-table indexes every nested group in the slice). FIRST-wins matches
    //    the prior `break`-on-first-same-slice semantics exactly: a failed
    //    build_nested_subview pushes a `table == nullptr` row, so a slice may
    //    hold a null row followed by a non-null one — taking the first keeps
    //    the build count identical (a stale null → one rebuild, as before).
    OffsetTable* table = nullptr;
    bool found_slice = false;
    for (auto const& row : nested_cache_) {
        if (row.slice_data != slice_data) {
            continue;
        }
        if (row.nested_no_tag == nested_no_tag) {
            return row.table != nullptr ? row.table->group_slices(nested_no_tag)
                                        : std::span<group_slice const>{};
        }
        if (!found_slice) {
            table = row.table;
            found_slice = true;
        }
    }
    if (table == nullptr) {
        table = build_nested_subview(slice_data, slice_len, resource(), opaque_dict,
                                     group_member_fn, gen);
    }
    try {
        nested_cache_.push_back(nested_cache_row{
            .slice_data = slice_data, .nested_no_tag = nested_no_tag, .table = table});
    } catch (std::bad_alloc const&) {
        // Cache insert failed; still serve this call from the built table —
        // degrade to "rebuild next time" rather than lose this result.
    }
    return table != nullptr ? table->group_slices(nested_no_tag) : std::span<group_slice const>{};
}

}  // namespace fixpp::wire
