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
#include <fixpp/wire/group_view.hpp>  // group_context complete type (063 T006/T008)
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
    :
#ifndef NDEBUG
      gen_{frame.token()},
#endif
      cfg_{},

      entries_(mr),
      overlay_(mr),
      group_slices_(mr),
      group_index_(mr),
      nested_cache_(mr) {
    build(frame);
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr,
                         void const* opaque_dict, group_member_fn_t group_member_fn,
                         group_delim_fn_t group_delim_fn) noexcept
    :
#ifndef NDEBUG
      gen_{frame.token()},
#endif
      cfg_{},
      opaque_dict_{opaque_dict},
      group_member_fn_{group_member_fn},
      group_delim_fn_{group_delim_fn},
      entries_(mr),
      overlay_(mr),
      group_slices_(mr),
      group_index_(mr),
      nested_cache_(mr) {
    build(frame);
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr,
                         Config cfg) noexcept
    :
#ifndef NDEBUG
      gen_{frame.token()},
#endif
      cfg_{cfg},

      entries_(mr),
      overlay_(mr),
      group_slices_(mr),
      group_index_(mr),
      nested_cache_(mr) {
    build(frame);
}

OffsetTable::OffsetTable(frame_view const& frame, std::pmr::memory_resource* mr, Config cfg,
                         void const* opaque_dict, group_member_fn_t group_member_fn,
                         group_delim_fn_t group_delim_fn) noexcept
    :
#ifndef NDEBUG
      gen_{frame.token()},
#endif
      cfg_{cfg},
      opaque_dict_{opaque_dict},
      group_member_fn_{group_member_fn},
      group_delim_fn_{group_delim_fn},
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
    check_alive();
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

void OffsetTable::check_alive() const noexcept {
#ifndef NDEBUG
    (void)frame_view_slice_access::make(frame_base_, 0, gen_).bytes();
#endif
}

// 063 T006: set/read the stored group_context (msg_type + bounded
// parent-no_tag path) — see offset_table.hpp for the ownership/seeding
// contract. Copies the constituent fields (never the `group_context` type
// itself, which offset_table.hpp only forward-declares).
void OffsetTable::set_group_context(group_context const& ctx) const noexcept {
    group_ctx_msg_type_ = ctx.msg_type;
    group_ctx_parent_path_ = ctx.parent_path;
    group_ctx_depth_ = ctx.depth;
}

group_context OffsetTable::stored_group_context() const noexcept {
    return group_context{.msg_type = group_ctx_msg_type_,
                         .parent_path = group_ctx_parent_path_,
                         .depth = group_ctx_depth_};
}

// 065 T003: public seed accessor for a C-ABI group cursor's OWN context
// (this table's stored context, pushed with `no_tag`) — see offset_table.hpp
// for the full contract. Out-of-line: `group_context` is only forward-declared
// in the header, so `.pushed()` needs the complete type (available here via
// group_view.hpp).
group_context OffsetTable::group_context_for(std::uint16_t no_tag) const noexcept {
    return stored_group_context().pushed(no_tag);
}

// 063 Defect B (T021): parse a NumInGroup count field's DECLARED value from
// the frame bytes via the shared saturating scanner (parse_bounded_u32,
// tag_scan.hpp). Non-digit bytes stop accumulation; the actual instance scan
// is separately bounded by entries_.size(), so a malformed/lying count can
// never over-consume.
static std::uint32_t parse_declared_count(std::byte const* base,
                                          OffsetTable::entry const& e) noexcept {
    return parse_bounded_u32(std::span<std::byte const>{base + e.offset, e.length});
}

std::size_t OffsetTable::consume_group_extent(std::size_t count_idx,
                                              group_context const& ctx,
                                              std::uint8_t depth,
                                              bool& overflow) const noexcept {
    if (depth >= kMaxGroupDepth) {
        overflow = true;  // T022: nesting deeper than K=16 -> err_group_too_large
        return count_idx;
    }
    std::size_t const first = count_idx + 1U;
    if (first >= entries_.size()) {
        return first;  // count field is last entry -> empty group
    }
    if (opaque_dict_ == nullptr || group_member_fn_ == nullptr) {
        return first;  // dict-free callers have no membership to walk
    }
    std::uint16_t const group_no_tag = entries_[count_idx].tag;
    std::uint16_t const delim = entries_[first].tag;
    // Confirm this count field really heads a group in-context (its delimiter
    // is a member); otherwise it is a plain scalar -> zero extent.
    if (!group_member_fn_(opaque_dict_, ctx, group_no_tag, delim)) {
        return first;
    }
    std::uint32_t const declared = parse_declared_count(frame_base_, entries_[count_idx]);
    if (declared == 0U) {
        return first;  // zero-count consumes no extent (B-004-7)
    }
    // Context under which THIS group's nested members are registered
    // (its container path + its own no_tag).
    group_context const child = ctx.pushed(group_no_tag);
    // fixpp#215 item 5: the "query before you advance" rule, stated ONCE.
    //
    // The walk below reaches a tag at two structurally different positions —
    // the instance-opening delimiter, and an ordinary member inside an instance
    // — and at BOTH it must ask the same question before stepping over it: is
    // this tag itself a nested group's count tag in the CHILD context? The test
    // is "the immediately-following entry is a member of the group this tag
    // would open". If yes, consume that nested group's FULL extent; if no,
    // advance one entry.
    //
    // Getting this wrong at either position produces the same Defect-B failure:
    // a bare `++k` past a nested count field leaves the walk inside the nested
    // group's instances, so the nested group's repeated delimiter is mistaken
    // for an outer-instance boundary and the outer extent truncates.
    //
    // Two degenerate cases fall through to the plain `at + 1U`, deliberately:
    // a tag that is the LAST entry (the `at + 1U < entries_.size()` bound), and
    // a nested group declaring 0 instances (which returns `at + 1` from the
    // zero-count arm above). Both behave exactly as they did before.
    //
    // The two call sites previously carried this logic inline, once each, with
    // the overflow check written only in the descent branch. Checking it after
    // BOTH branches is equivalent: `overflow` is false on entry (every setter
    // returns immediately) and the non-descent branch cannot set it.
    auto consume_one = [&](std::size_t at) noexcept -> std::size_t {
        if (at + 1U < entries_.size() &&
            group_member_fn_(opaque_dict_, child, entries_[at].tag, entries_[at + 1U].tag)) {
            return consume_group_extent(at, child, static_cast<std::uint8_t>(depth + 1U),
                                        overflow);
        }
        return at + 1U;  // ordinary tag — no nested descent
    };
    std::size_t k = first;
    std::uint32_t inst = 0;
    // Consume exactly `declared` instances (Approach A), each delimited by
    // `delim`; the scan is also hard-bounded by entries_.size() so a lying
    // declared count can never run off the end (fail-closed). Declared-vs-
    // actual mismatch flagging is the validator's job (plan.md).
    while (k < entries_.size() && inst < declared && entries_[k].tag == delim) {
        std::size_t const inst_start = k;
        // 083 C-8.0c: position 1 — the instance-opening delimiter.
        k = consume_one(k);
        if (overflow) {
            return k;  // at the cap, RETURN rather than burn `declared` no-op
                       // iterations (C-8.0c.3)
        }
        while (k < entries_.size() && entries_[k].tag != delim) {
            // Membership is checked against the OUTER group here (position 2 is
            // inside an instance): a non-member ends this instance, and the
            // group. Only once the tag is known to belong does `consume_one`
            // decide whether it also OPENS a nested group.
            if (!group_member_fn_(opaque_dict_, ctx, group_no_tag, entries_[k].tag)) {
                break;
            }
            // 083 C-8.0c: position 2 — an ordinary member inside an instance.
            k = consume_one(k);
            if (overflow) {
                return k;
            }
        }
        if ((k - inst_start) > cfg_.max_group_entries_per_instance) {
            overflow = true;  // T022: per-level entry cap breach
            return k;
        }
        ++inst;
    }
    return k;
}

core::expected_t<OffsetTable::group_index> OffsetTable::group(std::uint16_t no_tag) const noexcept {
    check_alive();
    if (!status_) {
        return fail<group_index>(status_.error());
    }
    // DICT-FREE CONSTRUCTION DECLINES (fixpp#220). Without a membership
    // oracle nothing in this frame can be established as a repeating group,
    // so this function does not answer for a dict-free table: it reports the
    // group ABSENT, which is the same answer the dict-aware path below gives
    // when the dictionary does not recognise `delim` as a member of `no_tag`.
    // group_slices() then yields an empty span and the C-ABI reports
    // TYPE_MISMATCH -- the documented E-2 / CA-010-read result ("not a
    // spurious group instance", 066 contracts/inbound-parse.md).
    //
    // WHAT THIS REPLACED. This branch used to bound the extent at
    // rest-of-message and cap each wire-delimiter-split instance over it.
    // That rule is the surviving fragment of the pre-`55b13459` heuristic
    // extent walk, whose end-of-message fallback was removed from the
    // dict-aware path as a P1 in that commit and never from this one. Kept
    // here it produced TWO defects from one cause: trailing top-level fields
    // were absorbed into the last instance -- reaching group_slices_status()
    // and so the typed group_view<GroupT>, not merely the cap -- and a
    // caller-tightened max_group_entries_per_instance could reject a
    // conforming frame outright.
    //
    // WHY DECLINING RATHER THAN A BETTER GUESS. There is no sound
    // wire-only rule: [2b 4.7] defines the boundary as the dictionary's
    // first-field-of-group rule per [FIX50SP2 3], so absent a dictionary the
    // boundary is undefined, not merely hard to compute. Both reference
    // engines decline for the same reason -- QuickFIX C++ returns from
    // Message::setGroup when DataDictionary::getGroup fails, forming no
    // Group at all; QuickFIX/J guards every parseGroup call site on a
    // non-null dictionary. Neither derives a group extent from the wire.
    if (opaque_dict_ == nullptr || group_member_fn_ == nullptr) {
        return err_required_field_missing<group_index>();
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
    // This table's stored membership context (msg_type + bounded parent
    // path). A ROOT table is seeded at `MessageView` construction time
    // (`MessageView::group<>()` re-applies the same value, idempotent);
    // a NESTED sub-table is seeded once by `build_nested_subview`.
    group_context const ctx = stored_group_context();
    // Validate that `no_tag` is actually a group count field by confirming
    // the dictionary recognises `delim` (the tag immediately following the
    // count field) as a member of `no_tag`'s group.  If the dict does NOT
    // know about this membership the count field is a plain scalar (e.g.
    // SenderCompID=49) and we must return an absent result so that
    // group_slices() yields an empty span → the thunk can report
    // TYPE_MISMATCH (E-2 / CA-010-read contract).
    if (!group_member_fn_(opaque_dict_, ctx, no_tag, delim)) {
        return err_required_field_missing<group_index>();
    }
    // 063 Defect B: nesting-aware extent. The pre-063 flat
    // `seen_in_instance` heuristic truncated a multi-entry NESTED group at
    // its 2nd entry (its repeated delimiter looked like a new outer
    // instance). consume_group_extent recursively consumes each nested
    // group's full declared extent so the outer slice encloses all of it.
    bool overflow = false;
    std::size_t const group_end = consume_group_extent(count_idx, ctx, ctx.depth, overflow);
    if (overflow) {
        return err_group_too_large<group_index>();
    }
    // 085: the flat per-instance cap loop that used to run here
    // unconditionally, after what was then a dict-aware/dict-free if/else,
    // was removed from the dictionary path. Why it could go: consume_group_extent's per-instance
    // cap check (`:521-524` as-of `ae65c36a`) already caps the same nesting-aware instances whose
    // extent its normal exit (`:527` as-of `ae65c36a`) returns; the flat partition that used to run
    // here merely refined that one, and consume_group_extent returns as soon as its own check
    // breaches — so the flat loop's cap comparison could never be the first to fire on this path.
    //
    // 220: the dict-free arm that loop was relocated INTO is now gone as
    // well — this function declines for a dict-free table at its entry
    // guard above, so there is no second arm and no second cap.
    // `OffsetTable::group()` is a dictionary-only operation.
    //
    // What stands in the flat loop's place (C-1, standing): this
    // function's entire per-instance DoS defence is now
    // consume_group_extent's cap over the instances whose extent it
    // returns. Any future change to that walk (the
    // instance-opening rule at `:477-478` as-of `ae65c36a`, the cap
    // check itself, or the delimiter consume_group_extent resolves at
    // `:458` as-of `ae65c36a`) MUST re-verify the cap still measures the
    // partition the function's return describes, or re-introduce an
    // independent per-instance cap in this function.
    return group_index{no_tag, first, group_end - first};
}

std::uint32_t OffsetTable::group_slices_reserve_bound() const noexcept {
    // 220: a dict-free table can never append to `group_slices_`, because
    // group() declines for it at this function's sibling guard and
    // group_slices_status() only pushes inside `if (gi)`. The bound is
    // therefore 0, not the old conservative `entries_.size()` — which reserved
    // up to 4096 * sizeof(group_slice) out of the fixed, null-upstream parse
    // arena for slices that provably cannot exist. That is the same
    // arena-exhaustion mode this bound was introduced to fix (PR #181 Tier-2
    // arena_fit), reappearing on the branch that lost its justification.
    //
    // ⚠️ PRECONDITION, not an observation: this 0 is correct only while group()
    // declines unconditionally without a membership oracle. If that is ever
    // relaxed, this MUST go back to `entries_.size()` — the reserve-once
    // contract above ("subsequent appends never reallocate, so every previously
    // returned span stays valid") is upheld here only because there are no
    // appends at all.
    if (opaque_dict_ == nullptr || group_member_fn_ == nullptr) {
        return 0U;
    }
    // Sum the DECLARED instance count of every TOP-LEVEL group count-field,
    // using the SAME predicate group() gates a push on: the dictionary
    // recognises entries_[e].tag as a group (under this table's ROOT context)
    // whose first member is the immediately following tag. So every tag that can
    // push into group_slices_ is counted here, and each contributes ≥ its actual
    // pushes (consume_group_extent caps instances at `declared`). Nested count
    // fields resolve under a different group_context and do not match the ROOT
    // ctx (their instances live in nested_cache_, not group_slices_). The
    // `parse_declared_count(e) > 0` prefilter prunes string/absent fields without
    // a membership call.
    group_context const ctx = stored_group_context();
    std::uint64_t total = 0;
    for (std::size_t e = 0; e + 1U < entries_.size(); ++e) {
        std::uint32_t const declared = parse_declared_count(frame_base_, entries_[e]);
        if (declared == 0U) {
            continue;
        }
        std::uint16_t const delim = entries_[e + 1U].tag;
        if (group_member_fn_(opaque_dict_, ctx, entries_[e].tag, delim)) {
            total += declared;
        }
    }
    // Clamp to entries_.size(): total instances ≤ total entries (each instance
    // needs ≥1 entry), so this is still a valid upper bound AND never exceeds the
    // old reserve — a huge/malicious declared count cannot inflate it.
    if (total > entries_.size()) {
        total = entries_.size();
    }
    return static_cast<std::uint32_t>(total);
}

// 073 T003: public span wrapper — UNCHANGED signature, one-line delegation.
// Every top-level caller (C-ABI top-level group getter, MessageView::group<>())
// keeps compiling and behaving identically (L-073-1, deferred).
std::span<group_slice const> OffsetTable::group_slices(std::uint16_t no_tag) const noexcept {
    return group_slices_status(no_tag).slices;
}

// 073 T003: status-bearing internal form (research.md §D2 mode (b), FR-002
// originate-at-failure). Body is the former `group_slices()` verbatim, with
// each exit widened to also report `alloc_failed`.
group_slices_result OffsetTable::group_slices_status(std::uint16_t no_tag) const noexcept {
    check_alive();
    // Already materialized for this no_tag — return the stable cached span.
    for (auto const& gs : group_index_) {
        if (gs.no_tag == no_tag) {
            return {.slices = {group_slices_.data() + gs.start, gs.count}, .alloc_failed = false};
        }
    }
    try {
        // Reserve once to a tight upper bound (sum of top-level declared counts,
        // clamped to entry count): subsequent appends never reallocate, so every
        // previously returned span stays valid. The old `entries_.size()` bound
        // over-reserved ~3x (fields, not instances) and exhausted the fixed
        // null-upstream parse arena on MSVC-release, silently degrading large
        // groups to empty (PR #181 Tier-2 arena_fit). See
        // group_slices_reserve_bound().
        if (!group_slices_reserved_) {
            group_slices_.reserve(group_slices_reserve_bound());
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
                // 083 T058 (C-8.2 / C-8.4): the boundary delimiter is now
                // resolved from the DICTIONARY, keyed on this table's STORED
                // context — `(msg_type, stored_group_context().parent_path,
                // no_tag)`, the SAME key the validator's descent uses, which is
                // what makes FR-021b's agreement structural rather than
                // coincidental.
                //
                // NOT `group_context_for(no_tag)`: that returns
                // `stored_group_context().pushed(no_tag)` (:424-426) and would
                // query one path element too long, violating Entity 1's
                // "parent_path EXCLUDES no_tag" invariant.
                //
                // C-8.4's fallback has exactly TWO cases and no third: no
                // dictionary / no callback -> today's wire-derived
                // `entries_[first].tag`; dictionary present -> the store's
                // answer, with no wire fallback (a zero means "not a group",
                // which callers already handle as absent).
                //
                // ⚠️ 220 CHANGED WHICH OF C-8.4's TWO CASES CAN RUN, and this
                // text used to justify the fallback as "behaviour-preserving,
                // and the CORRECT answer for a table with no dictionary". That
                // justification is now DEAD: a table with no dictionary cannot
                // reach this line at all, because group() declines for it and
                // this splitter only runs inside `if (gi)`. What survives is
                // the OTHER disjunct of the same guard — dictionary present but
                // `group_delim_fn_` null, which the dict-aware ctors still
                // default (offset_table.hpp / parser.hpp) — and that case was
                // never what the sentence above was written about. It is a
                // wire-derived rule running on a rationale that belonged to a
                // branch #220 removed: the same shape as #220 itself, one
                // callback over. No production construction reaches it (every
                // dict-aware site in src/ and include/ threads both callbacks;
                // Parser sets them together or not at all), but the fuzz
                // harness and several wire tests instantiate exactly that
                // shape. Deliberately NOT fixed here — a dict-aware behaviour
                // change does not belong in a dict-free-decline change. Tracked
                // separately; if you are here to change it, decide between
                // folding `group_delim_fn_ == nullptr` into group()'s decline
                // and dropping the `= nullptr` ctor defaults.
                // There is deliberately no "or the context did not resolve"
                // branch: the splitter cannot observe that state, since
                // `group_first_field` has already fallen through to the bare
                // global before it sees a value and returns a plain scalar with
                // no discriminator.
                std::uint16_t delim = entries_[first].tag;
                if (opaque_dict_ != nullptr && group_delim_fn_ != nullptr) {
                    group_context const ctx = stored_group_context();
                    if (std::uint16_t const d = group_delim_fn_(opaque_dict_, ctx, no_tag);
                        d != 0) {
                        delim = d;
                    }
                }
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
        return {.slices = {group_slices_.data() + start, count}, .alloc_failed = false};
    } catch (std::bad_alloc const&) {
        // 073 T003: this is the mode-(b) origin (FR-002) — the sub-table
        // built non-null but its own slice materialization exhausted the
        // arena. Degrade to "no instances", never throw (noexcept).
        return {.slices = {}, .alloc_failed = true};
    }
}

// 062 T005: dict-aware sub-view-over-slice builder (see offset_table.hpp for
// the ownership/lifetime/RC1 contract). Placement-constructs into `mr`,
// mirroring the established `mr->allocate(size, align)` + placement-new
// arena pattern (include/fixpp/core/sync/async_mutex.hpp:1160-1164).
OffsetTable* OffsetTable::build_nested_subview(
    std::byte const* data, std::size_t len, std::pmr::memory_resource* mr, void const* opaque_dict,
    group_member_fn_t group_member_fn, detail::generation_token gen, group_context const& ctx,
    group_delim_fn_t group_delim_fn) noexcept {
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
        // never the dict-free fallback. Placement-new into arena (`mr`) memory:
        // the sub-OffsetTable is owned by the per-message arena and freed with
        // it, not heap-owned (gsl::owner not adopted in this codebase).
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        // 083 T057 (C-8.1): the delimiter callback MUST be supplied here too. A
        // missed site would silently take C-8.4's dict-free fallback on nested
        // splits only -- the "context seeded lazily on ONE path leaves sibling
        // paths default" shape, invisible to any root-level test.
        auto* table = ::new (mem) OffsetTable(fv, mr, opaque_dict, group_member_fn, group_delim_fn);
        // 063 T008: seed the new sub-table's stored context VERBATIM (no
        // further push — see nested_group_slices()'s doc comment).
        table->set_group_context(ctx);
        return table;
    } catch (std::bad_alloc const&) {
        return nullptr;  // degrade: caller serves absent rather than throw
    }
}

// 073 (D2): resolve a built (or failed) nested sub-table pointer to the
// status-bearing result, OR-ing all THREE arena-exhaustion origins in ONE
// place so both empty-returning exits of nested_group_slices share a single
// definition — the two-exit drift this centralises is exactly the class of
// bug the T004 checklist audit caught (cache-hit vs final exit diverging).
//   (a) t == nullptr           — shell alloc failed / cached failed build;
//   (c) build_status() OOM     — ctor build() degraded to out_of_memory
//                                (offset_table.cpp:366-370); scoped to OOM so
//                                a malformed-data degradation stays not-failed
//                                (FR-007 disjointness);
//   (b) group_slices_status()  — the sub-table's own slice materialization
//                                caught bad_alloc (feedback_status_origin_
//                                must_cover_all_alloc_catch_sites).
static nested_slices_result resolve_nested_result(OffsetTable const* table,
                                                  std::uint16_t nested_no_tag) noexcept {
    if (table == nullptr) {
        return nested_slices_result{.slices = {}, .alloc_failed = true};  // mode (a)
    }
    bool const sub_build_oom =  // mode (c)
        !table->build_status() && table->build_status().error() == core::error::out_of_memory;
    auto const s = table->group_slices_status(nested_no_tag);  // mode (b)
    return nested_slices_result{.slices = s.slices, .alloc_failed = s.alloc_failed || sub_build_oom};
}

// 062 T006: single flat nested-subview cache (see offset_table.hpp for the
// full keying/ownership contract — ROOT-owned, keyed by
// `(slice_data, nested_no_tag)`, dedupes the sub-table build across distinct
// no_tags on the same slice).
nested_slices_result OffsetTable::nested_group_slices(
    std::byte const* slice_data, std::size_t slice_len, std::uint16_t nested_no_tag,
    void const* opaque_dict, group_member_fn_t group_member_fn, detail::generation_token gen,
    group_context const& ctx) const noexcept {
    if (slice_data == nullptr) {
        return nested_slices_result{.slices = {}, .alloc_failed = false};  // absent, not a failure
    }
    // Zero-length, alloc-free liveness check ([2b §6.4] INV-G6): the cache
    // scan below can return on a WARM (slice_data, nested_no_tag) hit
    // without ever touching `gen` again, so a stale token would otherwise be
    // served silently instead of fault-closing. `.bytes()` -> check_alive()
    // traps in debug on a stale token; no-op in release. Mirrors the mint at
    // ~:540 (build_nested_subview) but with len=0 so it never builds/allocs
    // — must not regress the FR-004b zero-alloc-on-repeat gate.
    (void)frame_view_slice_access::make(slice_data, 0, gen).bytes();
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
            // 073 T004 (D2 cache-hit exit): resolve against `row.table`, NOT
            // the function-scope local `table` — the local is still nullptr
            // for a same-no_tag match (only assigned from `row.table` in the
            // `!found_slice` branch below, which does not run for a matching
            // row). Using the local here would report alloc_failed=true on
            // every warm cache-hit of a healthy group (FR-006/FR-007
            // violation). All three origins are OR'd inside resolve_nested_result.
            return resolve_nested_result(row.table, nested_no_tag);
        }
        if (!found_slice) {
            table = row.table;
            found_slice = true;
        }
    }
    if (table == nullptr) {
        // 083 T057 (C-8.1): pass the delimiter callback through, so a NESTED
        // split resolves its boundary from the dictionary exactly as the root
        // does. Omitting it here would leave nested splits on the wire-derived
        // fallback while root splits used the store — the sibling-path drift
        // the contract calls out by name.
        table = build_nested_subview(slice_data, slice_len, resource(), opaque_dict,
                                     group_member_fn, gen, ctx, group_delim_fn_);
    }
    try {
        nested_cache_.push_back(nested_cache_row{
            .slice_data = slice_data, .nested_no_tag = nested_no_tag, .table = table});
    } catch (std::bad_alloc const&) {
        // Cache insert failed; still serve this call from the built table —
        // degrade to "rebuild next time" rather than lose this result.
    }
    // 073 T004 (D2 final exit): resolve against the post-build local `table`
    // (same three-origin OR as the cache-hit exit, shared via the helper).
    return resolve_nested_result(table, nested_no_tag);
}

// 065 T004: convenience overload — forwards to the 7-arg overload above using
// THIS table's own `opaque_dict_`/`group_member_fn_` and a build-mode-safe
// token (`token_for_nested_cache()`). The 7-arg algorithm + cache keying stay
// UNTOUCHED. Out-of-line: needs the complete `group_context` type (only
// forward-declared in the header).
nested_slices_result OffsetTable::nested_group_slices(std::byte const* slice_data,
                                                      std::size_t slice_len,
                                                      std::uint16_t nested_no_tag,
                                                      group_context const& ctx) const noexcept {
    return nested_group_slices(slice_data, slice_len, nested_no_tag, opaque_dict_,
                               group_member_fn_, token_for_nested_cache(), ctx);
}

}  // namespace fixpp::wire
