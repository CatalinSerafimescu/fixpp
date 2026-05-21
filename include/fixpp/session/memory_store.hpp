// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/memory_store.hpp
//
// fixpp::session::MemoryStore — in-memory MessageStore implementation.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.2. Entity E3. Catalogue S-012.
// FR-006 (MemoryStore contract) / FR-007 (zero-alloc under bounded) /
// FR-026 (peer PMR resource, not sub-resource) / FR-029 (no flush hook) /
// I-08 (capacity exhausted) / I-09 (no evict_oldest) / I-10 (zero alloc
// after construction under bounded).
//
// HEADER-ONLY per plan.md Project Structure line 187 (T021 decision):
// HALO-elision ([const §XI.6]) is friendlier to inline bodies; the 007/006
// precedent leaned header-inline for hot-path primitives.
//
// Fixed-slab layout under capacity_policy::bounded ([2e §4.2] line 486):
//   - ONE PMR allocation at ctor for the combined payload slab.
//   - TWO PMR allocations for the index vectors (reserved to capacity).
//   - store() performs ZERO allocator calls after construction (FR-007 / I-10).
// Under capacity_policy::unbounded:
//   - Growing PMR slab vector; allocs permitted per FR-007 bounded-only contract.
//
// capacity_policy::evict_oldest is UNREPRESENTABLE per [const §XV.15] (I-09).
//
// Mirror of specs/008-message-store/contracts/memory_store.hpp (shape oracle).
//
// NO std::mutex — [const §XV.9] / [SYN §3.2 Q6b]. Uses fixpp::sync::async_mutex
// (one per MemoryStore instance) per T040/US3. Overflow check on
// next_seqnum(_, true) at seqnum_max per FR-022 / I-18.
#pragma once

#include <asio/awaitable.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fixpp/core/error.hpp>
#include <fixpp/core/sync/async_mutex.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <span>
#include <vector>

namespace fixpp::session {

// Closed 2-value enum per contract (I-09; capacity-cap logic lands in T030).
// enum_extensibility(closed) + static_assert at every switch.
// `evict_oldest` is NOT a public name and NOT a numeric value (I-09).
enum class
#ifdef __clang__
    __attribute__((enum_extensibility(closed)))
#endif
    capacity_policy : std::uint8_t {
        bounded = 0,    // overflow → store_capacity_exhausted (I-08)
        unbounded = 1,  // grows without limit; test/embedded only
    };

class MemoryStore final : public MessageStore {
public:
    struct Config {
        capacity_policy policy = capacity_policy::bounded;
        std::size_t inbound_capacity = 10'000;
        std::size_t outbound_capacity = 10'000;
        std::size_t max_frame_bytes = 256 * 1024;             // 256 KiB
        std::pmr::memory_resource* store_resource = nullptr;  // null → engine-provided
    };

    explicit MemoryStore(Config cfg)
        : MessageStore(MessageStore::flush_thunk_for<MemoryStore>())  // nullptr per FR-029
          ,
          cfg_(cfg),
          mr_(cfg_.store_resource ? cfg_.store_resource : std::pmr::get_default_resource())
          // mr_ is declared before these members, so it is initialized when their ctors run.
          ,
          unbounded_slab_(std::pmr::polymorphic_allocator<std::byte>{mr_}),
          inbound_entries_(std::pmr::polymorphic_allocator<Entry>{mr_}),
          outbound_entries_(std::pmr::polymorphic_allocator<Entry>{mr_}) {
        if (cfg_.policy == capacity_policy::bounded) {
            // ONE PMR allocation for the fixed payload slab ([2e §4.2] line 486).
            // Layout: first inbound_capacity slots are inbound; next outbound_capacity slots are
            // outbound. Each slot is max_frame_bytes bytes.
            slab_total_bytes_ =
                (cfg_.inbound_capacity + cfg_.outbound_capacity) * cfg_.max_frame_bytes;
            if (slab_total_bytes_ > 0) {
                slab_ = static_cast<std::byte*>(
                    mr_->allocate(slab_total_bytes_, alignof(std::max_align_t)));
            }
            // Reserve index arrays — they NEVER reallocate after ctor (zero-alloc invariant).
            inbound_entries_.reserve(cfg_.inbound_capacity);
            outbound_entries_.reserve(cfg_.outbound_capacity);
        } else {
            // Unbounded: slab stays nullptr; growing PMR vector used for payloads.
            // Allocs permitted per FR-007 bounded-only zero-alloc contract.
        }
    }

    ~MemoryStore() override {
        if (slab_) {
            mr_->deallocate(slab_, slab_total_bytes_, alignof(std::max_align_t));
            slab_ = nullptr;
        }
    }

    // Not copyable or movable: slab pointer ownership is non-transferable.
    MemoryStore(const MemoryStore&) = delete;
    MemoryStore& operator=(const MemoryStore&) = delete;
    MemoryStore(MemoryStore&&) = delete;
    MemoryStore& operator=(MemoryStore&&) = delete;

    // ── store() ─────────────────────────────────────────────────────────────
    // Persist a single frame. Deep-copies frame bytes into store-owned storage.
    //
    // T040/US3: acquires async_mutex on entry; verifies seq == next_seqnum(dir,
    // false) inside the CS; deep-copies frame before any suspension; releases
    // mutex after copy is complete (before co_return).
    // NO std::mutex — [const §XV.9] / [SYN §3.2 Q6b].
    //
    // Leading asio::post: breaks the awaitable_thread::pump() recursive-call
    // chain that occurs when store() is called inside a tight coroutine loop.
    // async_mutex::async_lock()'s fast path fires the completion handler
    // synchronously (inside async_initiate's initiation lambda), which invokes
    // an inner pump() while the outer pump() is already running, causing stack
    // growth proportional to loop-iteration count. The leading post ensures each
    // entry into store() goes through the executor queue, capping recursion depth
    // to O(1) per call regardless of call count. Asio's internal thread-pool
    // allocator is used for the post — NOT the store's PMR resource — so the
    // zero-alloc guarantee (FR-007 / I-10) is preserved.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame [[clang::lifetimebound]],
        direction_t dir) noexcept override {
        // Yield through executor to break recursive pump() chain (see above).
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);

        // Acquire writer mutex — FIFO-fair per async_mutex contract.
        auto guard_result = co_await mutex_.async_lock();
        if (!guard_result) {
            // Mutex was cancelled/drained (shutdown path)
            co_return std::unexpected(fixpp::core::error::store_cancelled);
        }
        auto guard = std::move(*guard_result);  // holds the mutex

        // ── Critical section begins ───────────────────────────────────────
        auto& entries = entries_for(dir);
        const seqnum_t next = next_seq_for(dir);

        // Seqnum-order check (FR-018 / I-05) — inside CS.
        if (seq != next) {
            // guard releases mutex on destruction
            co_return std::unexpected(fixpp::core::error::store_seqnum_out_of_order);
        }

        // Capacity check for bounded policy
        if (cfg_.policy == capacity_policy::bounded) {
            const std::size_t cap =
                (dir == direction_t::inbound) ? cfg_.inbound_capacity : cfg_.outbound_capacity;
            if (entries.size() >= cap) {
                co_return std::unexpected(fixpp::core::error::store_capacity_exhausted);
            }
        }

        // Validate frame size
        if (frame.size() > cfg_.max_frame_bytes) {
            co_return std::unexpected(fixpp::core::error::store_io_failure);
        }

        // Deep-copy frame bytes BEFORE any suspension (I-02 / FR-019).
        Entry e;
        e.seq = seq;
        e.bytes_len = frame.size();

        if (cfg_.policy == capacity_policy::bounded) {
            // ── Slab path: ZERO allocations (FR-007 / I-10) ──────────────
            // Compute slot offset in the fixed slab and copy frame bytes.
            e.slab_offset = slab_offset_for(entries.size(), dir);
            std::memcpy(slab_ + e.slab_offset, frame.data(), frame.size());
            entries.push_back(e);  // reserved capacity — no realloc
        } else {
            // ── Unbounded path: growing PMR slab (allocs permitted) ───────
            e.slab_offset = unbounded_slab_.size();
            // insert() may reallocate the vector; existing offsets remain valid
            // for concurrent retrieve() calls because retrieve() now snapshots
            // the raw payload bytes UNDER the mutex (RC#1 fix) — it no longer
            // holds a raw pointer into unbounded_slab_ after mutex release.
            unbounded_slab_.insert(unbounded_slab_.end(), frame.begin(), frame.end());
            entries.push_back(e);
        }

        // Advance counter
        advance_next_seq_for(dir);

        // guard releases mutex here — CS is complete.
        co_return fixpp::core::expected_t<void>{};
    }

    // ── retrieve() ──────────────────────────────────────────────────────────
    // Walk [begin, end] in seqnum order (end == 0 → to current tail).
    //
    // T040/US3: acquires mutex ONCE to validate range + bulk-snapshot ALL entry
    // descriptors into a local vector; releases BEFORE any visitor co_await
    // (I-03 / FR-017). The per-frame re-acquire loop of the earlier design caused
    // unbounded awaitable_thread::pump() recursion (stack overflow at ~1000 frames).
    // Single-snapshot eliminates the loop entirely.
    //
    // Bounded-policy snapshot: copies only Entry descriptors (24 B each), then
    // reads payload bytes directly from the fixed slab without a per-frame copy.
    // Unbounded-policy snapshot: still copies Entry descriptors; payload bytes are
    // read from the growing unbounded_slab_ after mutex release (stable post-snap
    // because entries reference offsets into a vector that can grow under store(),
    // but the BYTES at existing offsets never change once written).
    //
    // PMR-throw boundary: visitor exceptions caught and routed to
    // store_visitor_aborted (T043 / I-21).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept override {
        // Validate begin (I-19: FIX seqnums start at 1)
        if (begin == 0) {
            co_return std::unexpected(fixpp::core::error::store_seqnum_invalid);
        }
        // Validate range (end != 0 && end < begin → store_invalid_range)
        if (end != 0 && end < begin) {
            co_return std::unexpected(fixpp::core::error::store_invalid_range);
        }

        // Single mutex acquisition: validate range + bulk-snapshot Entry descriptors.
        // Releasing the mutex BEFORE all visitor calls satisfies I-03 / FR-017:
        // "releases the mutex BEFORE invoking visitor.on_frame's co_await".
        //
        // For bounded: slab_ is stable (fixed allocation); reading slab_ + offset
        // after mutex release is safe — the slab is never reallocated.
        // For unbounded: we snapshot Entry descriptors (offsets + lengths); the
        // unbounded_slab_ data at those offsets is immutable once written (store()
        // only appends to the tail). Pointer recomputed at visitor time from
        // unbounded_slab_.data() — NOT cached across the mutex boundary.
        //
        // Gap handling: when a gap is detected mid-copy, we stop copying but
        // record the gap_hit flag. The visitor is still called for all frames
        // copied before the gap (spec: "already-visited frames are not re-visited;
        // iteration stops at the original end"), then gap error is returned.
        // N1 fix: bind snapshots to mr_ (the session PMR resource) so the
        // descriptor snapshot respects allocator discipline on the retrieve()
        // hot path. mr_ is already used by the slab and entry arrays; using it
        // here avoids a default-allocator heap allocation on each retrieve() call.
        // [const §VIII.5]: zero global-heap allocation between parse and fromApp.
        std::pmr::vector<Entry> snapshots{std::pmr::polymorphic_allocator<Entry>{mr_}};
        bool gap_hit = false;
        // RC#1 fix: for unbounded policy, copy raw payload bytes under the mutex
        // into this flat buffer. This eliminates the UAF/OOB hazard from the previous
        // approach of caching unbounded_slab_.data() before mutex release:
        // a concurrent store() may reallocate the vector, invalidating the pointer.
        // PMR allocations are permitted for unbounded policy (FR-007).
        std::pmr::vector<std::byte> unbounded_payload_copy{
            std::pmr::polymorphic_allocator<std::byte>{mr_}};
        {
            auto guard_result = co_await mutex_.async_lock();
            if (!guard_result) {
                co_return std::unexpected(fixpp::core::error::store_cancelled);
            }
            auto guard = std::move(*guard_result);

            const seqnum_t cur_next = next_seq_for(dir);
            const seqnum_t tail_end =
                (end == 0) ? (cur_next == seqnum_min ? 0 : cur_next - 1) : end;

            if (begin > tail_end || (tail_end < seqnum_min && end == 0)) {
                // No frames in range — empty-range success (guard releases)
                co_return fixpp::core::expected_t<void>{};
            }

            const auto& entries = entries_for(dir);
            snapshots.reserve(static_cast<std::size_t>(tail_end - begin + 1));

            if (cfg_.policy == capacity_policy::unbounded) {
                // Reserve to avoid repeated reallocations during the copy loop.
                const std::size_t range = static_cast<std::size_t>(tail_end - begin + 1);
                unbounded_payload_copy.reserve(range * cfg_.max_frame_bytes);
            }

            for (seqnum_t s = begin; s <= tail_end; ++s) {
                std::size_t idx = static_cast<std::size_t>(s - 1);
                if (idx >= entries.size() || entries[idx].seq != s) {
                    // Gap detected: stop copying, record flag, break.
                    // Frames copied so far (snapshots) will still be visited.
                    gap_hit = true;
                    break;
                }
                Entry e = entries[idx];
                if (cfg_.policy == capacity_policy::unbounded) {
                    // RC#1: copy payload bytes now while the mutex is held and the
                    // slab pointer is guaranteed stable. Reuse e.slab_offset to
                    // record the byte offset into unbounded_payload_copy.
                    const std::byte* src = unbounded_slab_.data() + entries[idx].slab_offset;
                    const std::size_t copy_start = unbounded_payload_copy.size();
                    unbounded_payload_copy.insert(unbounded_payload_copy.end(),
                                                  src, src + e.bytes_len);
                    e.slab_offset = copy_start;
                }
                snapshots.push_back(e);
            }
            // guard releases mutex here — BEFORE all visitor co_awaits (I-03)
        }

        // Walk pre-snapshotted entry descriptors WITHOUT holding mutex (I-03 / FR-017):
        //   allows concurrent store() calls from within the visitor body.
        // T043: wrap visitor call in try/catch → store_visitor_aborted (I-21).
        for (const auto& e : snapshots) {
            // Resolve payload span from stable storage (no per-frame alloc after this).
            // For bounded: slab_ is a fixed PMR allocation that is never reallocated.
            // For unbounded: unbounded_payload_copy holds the bytes snapshotted under mutex.
            const std::byte* payload_base = (cfg_.policy == capacity_policy::bounded)
                                                ? slab_
                                                : unbounded_payload_copy.data();
            std::span<const std::byte> frame_view{payload_base + e.slab_offset, e.bytes_len};

            fixpp::core::expected_t<visit_result> vr{visit_result::cont};
            try {
                vr = co_await visitor.on_frame(e.seq, frame_view);
            } catch (...) {
                // PMR or any other exception from visitor → store_visitor_aborted
                // (I-21 / T043). Boundary: catch, return error, no terminate.
                co_return std::unexpected(fixpp::core::error::store_visitor_aborted);
            }

            if (!vr) {
                co_return std::unexpected(vr.error());
            }
            switch (*vr) {
                case visit_result::cont:
                    continue;
                case visit_result::stop:
                    co_return fixpp::core::expected_t<void>{};
                case visit_result::abort:
                    co_return std::unexpected(visitor.abort_error());
            }
        }

        // If a gap was detected during the bulk-copy phase, report it now —
        // AFTER the visitor has been called for all pre-gap frames (spec I-19:
        // "already-visited frames are not re-visited").
        if (gap_hit) {
            co_return std::unexpected(fixpp::core::error::store_seqnum_gap);
        }

        co_return fixpp::core::expected_t<void>{};
    }

    // ── next_seqnum() ────────────────────────────────────────────────────────
    // Read (increment=false) or read-then-increment (increment=true).
    //
    // T040/US3: acquires mutex. Overflow check at seqnum_max → store_seqnum_overflow
    // without incrementing (FR-022 / I-18 — session-fatal; store does NOT reset).
    // Leading post breaks recursive pump() chain (same rationale as store()).
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto guard_result = co_await mutex_.async_lock();
        if (!guard_result) {
            co_return std::unexpected(fixpp::core::error::store_cancelled);
        }
        auto guard = std::move(*guard_result);

        seqnum_t& counter = counter_for(dir);
        const seqnum_t current = counter;
        if (increment) {
            // Overflow check (FR-022 / I-18): seqnum_max is session-fatal.
            if (current == seqnum_max) {
                co_return std::unexpected(fixpp::core::error::store_seqnum_overflow);
            }
            ++counter;
        }
        co_return fixpp::core::expected_t<seqnum_t>{current};
    }

    // ── reset() ─────────────────────────────────────────────────────────────
    // Clear all frames (both directions) and rewind counters to 1.
    // T040/US3: acquires writer mutex before clearing.
    // Leading post breaks recursive pump() chain (same rationale as store()).
    //
    // Bounded: clear() on reserved vectors preserves capacity — no realloc.
    // Slab buffer is NOT deallocated; new store() calls overwrite from slot 0.
    // Unbounded: clear() + shrink_to_fit() on unbounded_slab_ to release memory.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        co_await asio::post(co_await asio::this_coro::executor, asio::use_awaitable);
        auto guard_result = co_await mutex_.async_lock();
        if (!guard_result) {
            co_return std::unexpected(fixpp::core::error::store_cancelled);
        }
        auto guard = std::move(*guard_result);

        inbound_entries_.clear();
        outbound_entries_.clear();
        if (cfg_.policy == capacity_policy::unbounded) {
            unbounded_slab_.clear();
        }
        next_inbound_ = seqnum_min;
        next_outbound_ = seqnum_min;
        co_return fixpp::core::expected_t<void>{};
    }

private:
    // ── Internal types ───────────────────────────────────────────────────────

    // Entry descriptor: identifies a frame by seqnum and its location in the slab.
    //
    // Layout analysis (per [2e §4.2] "16 B canonical" target):
    //   seqnum_t  (uint32_t) = 4 B
    //   bytes_len (size_t)   = 8 B  (payload length ≤ max_frame_bytes ≤ 256 KiB)
    //   slab_offset (size_t) = 8 B  (offset within slab; ≤ (in+out)×max_frame_bytes)
    //   Total: 20 B, padded by compiler to 24 B on typical 64-bit ABI.
    //
    // The design doc's "16 B canonical" was written assuming uint32_t for both
    // bytes_len and slab_offset. Using size_t (8 B each) gives 24 B but avoids
    // any truncation risk for engines with large slab sizes. The static_assert
    // below enforces ≤ 32 B (design doc bound is soft, 16 B is not ABI-frozen).
    struct Entry {
        seqnum_t seq{};
        std::size_t bytes_len{};    // payload length in slab
        std::size_t slab_offset{};  // byte offset into the slab buffer
    };
    static_assert(
        sizeof(Entry) <= 32,
        "Entry must stay <= 32 B per [2e §4.2] (16 B canonical, 24 B actual with size_t fields)");

    // ── Helpers ──────────────────────────────────────────────────────────────

    // Compute the slab offset for the next entry slot in the given direction's vector.
    // Called under the mutex during store().
    [[nodiscard]] std::size_t slab_offset_for(std::size_t local_idx,
                                              direction_t dir) const noexcept {
        const std::size_t global_slot =
            (dir == direction_t::inbound) ? local_idx : cfg_.inbound_capacity + local_idx;
        return global_slot * cfg_.max_frame_bytes;
    }

    std::pmr::vector<Entry>& entries_for(direction_t dir) noexcept {
        return (dir == direction_t::inbound) ? inbound_entries_ : outbound_entries_;
    }
    const std::pmr::vector<Entry>& entries_for(direction_t dir) const noexcept {
        return (dir == direction_t::inbound) ? inbound_entries_ : outbound_entries_;
    }

    seqnum_t next_seq_for(direction_t dir) const noexcept {
        return (dir == direction_t::inbound) ? next_inbound_ : next_outbound_;
    }

    seqnum_t& counter_for(direction_t dir) noexcept {
        return (dir == direction_t::inbound) ? next_inbound_ : next_outbound_;
    }

    void advance_next_seq_for(direction_t dir) noexcept {
        if (dir == direction_t::inbound) {
            ++next_inbound_;
        } else {
            ++next_outbound_;
        }
    }

#ifdef FIXPP_TEST_HOOKS
public:
    // Test-only: force a seqnum counter to a specific value.
    // PRECONDITION: called single-threaded (no concurrent store/retrieve calls).
    void test_set_counter(direction_t dir, seqnum_t value) noexcept {
        if (dir == direction_t::inbound) {
            next_inbound_ = value;
        } else {
            next_outbound_ = value;
        }
    }

private:
#endif  // FIXPP_TEST_HOOKS

    // ── State ────────────────────────────────────────────────────────────────

    Config cfg_;

    // PMR resource resolved at ctor: cfg_.store_resource ?? get_default_resource().
    std::pmr::memory_resource* mr_;

    // Per-instance writer mutex (T040/US3). FIFO-fair via async_mutex.
    // NO std::mutex — [const §XV.9] / [SYN §3.2 Q6b].
    // Declared BEFORE entry vectors so destruction order is correct:
    // mutex must still be valid when entries are accessed during drain.
    fixpp::sync::async_mutex mutex_;

    // ── Bounded-policy slab ([2e §4.2] line 486) ─────────────────────────
    // ONE PMR allocation at ctor for the fixed-size payload slab.
    // Layout: slots [0, inbound_capacity) = inbound, [inbound_capacity, total) = outbound.
    // Each slot is exactly max_frame_bytes bytes.
    // slab_ == nullptr when policy == unbounded OR (inbound+outbound)×max_frame_bytes == 0.
    std::byte* slab_{nullptr};
    std::size_t slab_total_bytes_{0};

    // ── Unbounded-policy slab ─────────────────────────────────────────────
    // Growing PMR-backed byte vector; empty under bounded policy.
    // Allocs permitted per FR-007 bounded-only contract.
    // store() appends to tail; retrieve() copies payload bytes under the
    // mutex (RC#1 fix) rather than dereferencing a stale base pointer —
    // a concurrent store() can reallocate this vector via insert(), invalidating
    // any data() pointer captured before mutex release.
    // Initialized in ctor initializer list with mr_ allocator.
    std::pmr::vector<std::byte> unbounded_slab_;

    // ── Index arrays ──────────────────────────────────────────────────────
    // PMR-backed Entry descriptor vectors.
    // Bounded: reserved to capacity at ctor — push_back never reallocates.
    // Unbounded: empty at ctor, grow on demand (allocs permitted).
    std::pmr::vector<Entry> inbound_entries_;
    std::pmr::vector<Entry> outbound_entries_;

    seqnum_t next_inbound_{seqnum_min};   // next expected inbound seqnum
    seqnum_t next_outbound_{seqnum_min};  // next expected outbound seqnum
};

// Static asserts for the visit_result switch coverage guard
static_assert(static_cast<std::uint8_t>(visit_result::cont) == 0);
static_assert(static_cast<std::uint8_t>(visit_result::stop) == 1);
static_assert(static_cast<std::uint8_t>(visit_result::abort) == 2);

// Static asserts for capacity_policy closed-enum guard
static_assert(static_cast<std::uint8_t>(capacity_policy::bounded) == 0);
static_assert(static_cast<std::uint8_t>(capacity_policy::unbounded) == 1);

}  // namespace fixpp::session
