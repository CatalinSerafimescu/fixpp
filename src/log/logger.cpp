// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/log/logger.cpp
//
// Logger::Impl — bounded power-of-2 MPSC ring + dedicated drain OS thread.
// Implements T025 (ring + drain) + T026 (fan-out catch-all).
//
// Anchors:
//   [2k §4.3]           — MPSC ring protocol (write_sequence_/read_sequence_
//                          ordering; overflow load-check-CAS; drain thread)
//   contracts/log-core.md — runtime obligations (zero alloc, noexcept enqueue,
//                            overflow drop_newest, category filter, separate counters)
//   [const §VIII.5]     — zero allocation between parse and fromApp
//   [const §XIII.2]     — drop_newest preserves oldest; bounded drop counter
//
// ── MPSC ring protocol (§4.3 P1-2 fix implemented) ──────────────────────────
//
// write_sequence_ (producers CAS acq_rel/relaxed) — tracks the NEXT slot to claim.
// read_sequence_  (drain release store; producers relaxed load) — tracks the
//                  NEXT slot the drain will consume.
//
// Each ring slot has an `alignas(64) std::atomic<uint64_t> sequence` beside the
// Record.  The protocol per slot at index S (S = claimed slot number):
//   Producer writes:  slot[S % cap].sequence.store(S + 1, release) after writing.
//   Drain reads when: slot[r % cap].sequence.load(acquire) == r + 1; then
//                     copies the Record, then read_sequence_.store(r+1, release).
//
// Overflow check (load-check-CAS, R5 — never overwrites an unread slot):
//   1. Load w = write_sequence_.load(relaxed).
//   2. Load r = read_sequence_.load(relaxed).
//   3. If w - r >= capacity → overflow: drop (increment drop_count_); return.
//   4. CAS write_sequence_(w → w+1, acq_rel/relaxed).
//   5. CAS fail → retry from step 1.
//   6. CAS success → producer owns slot (w % capacity); write Record, then
//      slot[w % cap].sequence.store(w + 1, release).
//
// Key correctness properties:
// - Step 3 checks BEFORE claiming a slot.  write_sequence_ is never advanced
//   on overflow so the drain never waits on a slot that was claimed-but-not-written.
// - Relaxed load of read_sequence_ in step 2: a stale (under-advanced) read makes
//   the ring look fuller than it is → at worst causes an early drop_newest drop.
//   Safe because drop_newest is the defined behaviour; no corruption possible.
// - The per-slot sequence atomic prevents the drain from reading a partially-written
//   slot: the producer stores sequence = w+1 AFTER writing the Record (release),
//   the drain reads with acquire semantics, so the full Record write is visible.
// - TSan-clean: all accesses to write_sequence_ and read_sequence_ are through
//   std::atomic<uint64_t> with correct memory_order tags.

#include <fixpp/log/logger.hpp>
#include <fixpp/log/format_registry.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory_resource>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#endif

namespace {
// Sentinel marker stored in Record::flags.
// A record with this flag is a flush sentinel, not a real log record.
inline constexpr std::uint8_t k_flush_sentinel_flag = 0xFF;
}  // namespace

namespace fixpp::log {

// ── Ring slot ──────────────────────────────────────────────────────────────
//
// Each slot holds one Record plus a 64-byte-aligned sequence counter.
// The sequence counter is placed on a SEPARATE cache line from write_sequence_
// and read_sequence_ to avoid false sharing.
struct alignas(64) RingSlot {
    // sequence: producer writes (w+1, release); drain reads with acquire.
    // Initially = slot_index (meaning "empty, available for slot_index").
    std::atomic<std::uint64_t> sequence;
    Record                      record;
};

// ── Logger::Impl ───────────────────────────────────────────────────────────

struct Logger::Impl {
    // ── Configuration ────────────────────────────────────────────────────
    LoggerConfig                             config_;
    std::pmr::vector<std::unique_ptr<Sink>>  sinks_;

    // ── Ring buffer ───────────────────────────────────────────────────────
    // Allocated from config_.ring_resource at construction.
    // capacity_ is a power-of-2 copy of config_.capacity (validated on ctor).
    std::uint64_t               capacity_;
    RingSlot*                   ring_;         // raw PMR-allocated array

    // ── Sequence counters — each on its own cache line ───────────────────
    // Producer: CAS acq_rel/relaxed.  Drain: reads relaxed for slot index.
    alignas(64) std::atomic<std::uint64_t> write_sequence_{0};
    // Drain: release store after consuming.  Producer: relaxed load (overflow check).
    alignas(64) std::atomic<std::uint64_t> read_sequence_{0};

    // ── Filter mask ───────────────────────────────────────────────────────
    // Bit (category & 63u) set ⇒ category enabled.  Initial: all bits set.
    alignas(64) std::atomic<std::uint64_t> enabled_categories_mask_{~std::uint64_t{0}};

    // ── Counters (separate atomics per contracts/log-core.md) ─────────────
    alignas(64) std::atomic<std::uint64_t> drop_count_{0};
    alignas(64) std::atomic<std::uint64_t> timeout_drop_count_{0};
    alignas(64) std::atomic<std::uint64_t> filter_count_{0};

    // Per-sink error counter (indexed by sink position; sized at construction).
    // std::atomic is non-copyable/movable so we use a heap array.
    std::size_t                              num_sinks_{0};
    std::unique_ptr<std::atomic<std::uint64_t>[]> sink_error_counts_;

    // ── Flush-sentinel completion queue ──────────────────────────────────────
    // Protected by flush_mutex_. The drain thread pops under the lock, then
    // invokes the completion OUTSIDE the lock to avoid re-entrancy risk.
    std::mutex                          flush_mutex_;
    std::queue<std::function<void()>>   flush_queue_;

    // ── Drain-done notification (for shutdown timeout) ────────────────────────
    // The drain thread sets drain_done_ = true and notifies drain_done_cv_
    // just before drain_loop() returns. shutdown() uses wait_for().
    std::mutex               drain_done_mutex_;
    std::condition_variable  drain_done_cv_;
    bool                     drain_done_{false};

    // ── Drain thread lifecycle ─────────────────────────────────────────────
    std::atomic<bool> stop_flag_{false};
    std::thread       drain_thread_;

    // ── Ctor / Dtor ───────────────────────────────────────────────────────

    explicit Impl(LoggerConfig config,
                  std::pmr::vector<std::unique_ptr<Sink>> sinks)
        : config_{config}
        , sinks_{std::move(sinks)}
        , capacity_{config_.capacity}
        , ring_{nullptr}
    {
        // Validate power-of-2 capacity.
        assert(capacity_ >= 1u && (capacity_ & (capacity_ - 1u)) == 0u &&
               "LoggerConfig::capacity must be a power of 2");

        // Allocate ring from PMR resource.
        void* mem = config_.ring_resource->allocate(
            capacity_ * sizeof(RingSlot), alignof(RingSlot));
        ring_ = reinterpret_cast<RingSlot*>(mem);

        // Initialise each slot's sequence to its own index (= "empty").
        for (std::uint64_t i = 0; i < capacity_; ++i) {
            // Use placement-new for proper atomic initialisation.
            new (&ring_[i]) RingSlot{};
            ring_[i].sequence.store(i, std::memory_order_relaxed);
        }

        // Initialise per-sink error counters (atomic array; non-copyable).
        num_sinks_ = sinks_.size();
        sink_error_counts_ = std::make_unique<std::atomic<std::uint64_t>[]>(num_sinks_);
        for (std::size_t i = 0; i < num_sinks_; ++i) {
            sink_error_counts_[i].store(0, std::memory_order_relaxed);
        }

        // Open each sink (drain thread not yet running; single-threaded).
        for (std::size_t i = 0; i < sinks_.size(); ++i) {
            if (sinks_[i]) {
                auto result = sinks_[i]->open();
                if (!result.has_value()) {
                    // Sink failed to open — disable it (set to null).
                    // Increment its error counter so callers can detect the failure.
                    sink_error_counts_[i].fetch_add(1, std::memory_order_relaxed);
                    sinks_[i].reset();
                }
            }
        }

        // Spawn the dedicated drain OS thread.
        // The drain thread holds no session/engine references per [2k §4.3].
        drain_thread_ = std::thread([this]{ drain_loop(); });

        // Optional CPU affinity hint (Linux only).
#ifdef __linux__
        if (config_.drain_cpu_affinity >= 0) {
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(config_.drain_cpu_affinity, &cpuset);
            pthread_setaffinity_np(
                drain_thread_.native_handle(), sizeof(cpuset), &cpuset);
        }
#endif
    }

    ~Impl()
    {
        // Signal the drain thread to stop after flushing remaining records.
        stop_flag_.store(true, std::memory_order_release);
        if (drain_thread_.joinable()) {
            drain_thread_.join();
        }

        // Close all sinks (drain thread is gone; single-threaded from here).
        for (auto& sink : sinks_) {
            if (sink) {
                sink->close();
            }
        }

        // Return ring memory to PMR resource.
        if (ring_) {
            // Call dtors of RingSlot (atomics need to be destroyed properly).
            for (std::uint64_t i = 0; i < capacity_; ++i) {
                ring_[i].~RingSlot();
            }
            config_.ring_resource->deallocate(
                ring_, capacity_ * sizeof(RingSlot), alignof(RingSlot));
            ring_ = nullptr;
        }
    }

    // ── Producer: enqueue ─────────────────────────────────────────────────
    //
    // Load-check-CAS overflow protocol per §4.3 / R5.
    // noexcept — never throws; drops on overflow.
    void enqueue(Level                                level,
                 Category                             category,
                 std::uint32_t                        format_id,
                 std::array<std::uint8_t, 16> const&  trace_id,
                 std::uint64_t                        span_id,
                 fixpp::core::utc_time_point          timestamp,
                 std::initializer_list<ArgValue>      args) noexcept
    {
        // ── Runtime category filter ────────────────────────────────────────
        // Check BEFORE doing anything with the ring.
        // Bit index = category & 63u (no UB for any uint16 value).
        auto const bit_index = static_cast<std::uint64_t>(category & 63u);
        auto const mask = enabled_categories_mask_.load(std::memory_order_relaxed);
        if (!(mask & (std::uint64_t{1} << bit_index))) {
            filter_count_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        // ── MPSC ring load-check-CAS ───────────────────────────────────────
        for (;;) {
            // Step 1: load current write position (relaxed — we will CAS it).
            std::uint64_t w = write_sequence_.load(std::memory_order_relaxed);

            // Step 2: load drain position with relaxed ordering.
            // A stale (under-advanced) read makes the ring look fuller → early drop.
            // Safe under drop_newest (contracts/log-core.md §Runtime obligations).
            std::uint64_t r = read_sequence_.load(std::memory_order_relaxed);

            // Step 3: overflow check BEFORE claiming a slot (R5).
            if (w - r >= capacity_) {
                if (config_.on_overflow == overflow_policy::block) {
                    // block mode: spin-yield until a slot becomes available.
                    // MUST NOT be called from a session-strand coroutine ([const §XI.3]).
                    // TODO(T033/T034): Add session-strand thread detection here and
                    // fire a debug assert if block is called from the session executor.
                    // For now: unconditional spin-yield is correct for raw-thread
                    // producer paths (FR-004 / TS-3).
                    std::this_thread::yield();
                    continue;
                }
                // drop_newest: drop this record, count it, return immediately.
                drop_count_.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            // Step 4: CAS write_sequence_ (w → w+1, acq_rel on success).
            if (!write_sequence_.compare_exchange_weak(
                    w, w + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                // Another producer won; retry.
                continue;
            }

            // Step 5: CAS success — we own slot w.
            // Write the Record into ring_[w % capacity_].
            // The drain will not read this slot until we store sequence = w+1.
            RingSlot& slot = ring_[w % capacity_];

            // Build the record.
            Record& rec = slot.record;
            rec.timestamp  = timestamp;
            rec.trace_id   = trace_id;
            rec.span_id    = span_id;
            rec.level      = level;
            rec.flags      = 0;
            rec.category   = category;
            rec.format_id  = format_id;

            // Copy at most k_max_args ArgValues by value (contracts/log-core.md New-3).
            // The initializer_list backing array is stack-allocated by the compiler;
            // we copy by value — no dynamic container, no heap allocation.
            auto const n = std::min(args.size(), k_max_args);
            rec.arg_count = static_cast<std::uint8_t>(n);
            std::size_t idx = 0;
            for (auto const& av : args) {
                if (idx >= n) break;
                rec.args[idx++] = av;
            }
            // Zero-init remaining arg slots (defence-in-depth).
            for (std::size_t j = idx; j < k_max_args; ++j) {
                rec.args[j] = ArgValue{};
            }

            // Step 6: signal to the drain that this slot is ready.
            // Release store so the drain (with acquire load) sees the full Record.
            slot.sequence.store(w + 1, std::memory_order_release);
            return;
        }
    }

    // ── Drain thread loop ─────────────────────────────────────────────────
    //
    // Dedicated OS thread. Not an asio strand. Holds no session/engine refs.
    // Reads records in read_sequence_ order. For each consumed record:
    //   1. If rec.flags == k_flush_sentinel_flag: pop one completion from
    //      flush_queue_ and invoke it (async_flush protocol).
    //   2. Otherwise: look up the format string via format_registry, fan-out
    //      to sinks (T026 FR-005).
    void drain_loop()
    {
        std::uint64_t r = 0;  // local copy of read_sequence_ (drain owns it)

        while (true) {
            // Check if this slot is ready.
            RingSlot& slot = ring_[r % capacity_];
            std::uint64_t seq = slot.sequence.load(std::memory_order_acquire);

            if (seq == r + 1) {
                // Slot ready — copy the record (local copy for safe fan-out).
                Record rec = slot.record;

                // Fan-out to sinks BEFORE advancing read_sequence_.
                // Advancing AFTER emit() ensures the slot remains "occupied"
                // (from the producer's overflow-check perspective) for the full
                // duration of the emit() call.  This is the behaviour required
                // by the drop_newest contract: with capacity=1 and a blocking
                // emit(), ALL subsequent enqueues see a full ring and are dropped
                // rather than racing for the freed slot.
                // Spec §4.3: "consume the record; advance read_sequence_" —
                // consume = emit() complete; then advance.

                if (rec.flags == k_flush_sentinel_flag) {
                    // Flush sentinel: pop and invoke the matching completion.
                    std::function<void()> completion;
                    {
                        std::lock_guard<std::mutex> lock(flush_mutex_);
                        if (!flush_queue_.empty()) {
                            completion = std::move(flush_queue_.front());
                            flush_queue_.pop();
                        }
                    }
                    // Invoke OUTSIDE the lock to avoid re-entrancy.
                    if (completion) {
                        try {
                            completion();
                        } catch (...) {
                            // Swallow; completion must not throw.
                        }
                    }
                } else {
                    fan_out(rec);
                }

                // Advance read_sequence_ (release store).
                read_sequence_.store(r + 1, std::memory_order_release);
                ++r;

            } else {
                // No record available yet.
                if (stop_flag_.load(std::memory_order_acquire)) {
                    // Drain remaining slots before exiting.
                    // We loop until the ring is empty (read catches write).
                    std::uint64_t w = write_sequence_.load(std::memory_order_acquire);
                    if (r >= w) {
                        break;  // ring empty; done
                    }
                    // Else there are still slots to drain — keep spinning.
                }
                std::this_thread::yield();
            }
        }

        // Flush all sinks after draining (with the configured drain_timeout).
        for (std::size_t i = 0; i < sinks_.size(); ++i) {
            if (sinks_[i]) {
                try {
                    sinks_[i]->flush(config_.drain_timeout);
                } catch (...) {
                    if (i < num_sinks_) {
                        sink_error_counts_[i].fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }
        }

        // Signal that the drain is done (for shutdown timeout notification).
        {
            std::lock_guard<std::mutex> lock(drain_done_mutex_);
            drain_done_ = true;
        }
        drain_done_cv_.notify_all();
    }

    // T026: fan-out catch-all.
    // Wrap each Sink::emit in try/catch; increment sink_error_count(i) on throw;
    // continue — one failing sink never stalls the drain or the others (FR-005).
    void fan_out(Record const& rec) noexcept
    {
        for (std::size_t i = 0; i < sinks_.size(); ++i) {
            if (!sinks_[i]) continue;
            try {
                sinks_[i]->emit(rec);
            } catch (...) {
                // Increment per-sink error counter (log_sink_write_failed semantics).
                if (i < num_sinks_) {
                    sink_error_counts_[i].fetch_add(1, std::memory_order_relaxed);
                }
                // Continue to next sink — one sink failing must not stall others.
            }
        }
    }
};

// ── Logger public API ─────────────────────────────────────────────────────

Logger::Logger(LoggerConfig config,
               std::pmr::vector<std::unique_ptr<Sink>> sinks)
    : impl_{std::make_unique<Impl>(std::move(config), std::move(sinks))}
{}

Logger::~Logger() = default;

void Logger::enqueue(Level                                level,
                     Category                             category,
                     std::uint32_t                        format_id,
                     std::array<std::uint8_t, 16> const&  trace_id,
                     std::uint64_t                        span_id,
                     fixpp::core::utc_time_point          timestamp,
                     std::initializer_list<ArgValue>      args) noexcept
{
    impl_->enqueue(level, category, format_id, trace_id, span_id, timestamp, args);
}

void Logger::set_category_enabled(Category cat, bool enabled) noexcept
{
    auto const bit = std::uint64_t{1} << (cat & 63u);
    if (enabled) {
        impl_->enabled_categories_mask_.fetch_or(bit, std::memory_order_relaxed);
    } else {
        impl_->enabled_categories_mask_.fetch_and(~bit, std::memory_order_relaxed);
    }
}

bool Logger::is_category_enabled(Category cat) const noexcept
{
    auto const bit = std::uint64_t{1} << (cat & 63u);
    return (impl_->enabled_categories_mask_.load(std::memory_order_relaxed) & bit) != 0;
}

std::uint64_t Logger::drop_count() const noexcept
{
    return impl_->drop_count_.load(std::memory_order_relaxed);
}

std::uint64_t Logger::timeout_drop_count() const noexcept
{
    return impl_->timeout_drop_count_.load(std::memory_order_relaxed);
}

std::uint64_t Logger::filter_count() const noexcept
{
    return impl_->filter_count_.load(std::memory_order_relaxed);
}

void Logger::reset_drop_count() noexcept
{
    impl_->drop_count_.store(0, std::memory_order_relaxed);
}

void Logger::reset_timeout_drop_count() noexcept
{
    impl_->timeout_drop_count_.store(0, std::memory_order_relaxed);
}

void Logger::reset_filter_count() noexcept
{
    impl_->filter_count_.store(0, std::memory_order_relaxed);
}

std::uint64_t Logger::sink_error_count(std::size_t sink_index) const noexcept
{
    if (sink_index >= impl_->num_sinks_) return 0u;
    return impl_->sink_error_counts_[sink_index].load(std::memory_order_relaxed);
}

// shutdown() — T027.
// [2k §4.3] / contracts/log-core.md FR-014 / SC-007.
//
// Protocol:
//   1. Idempotent: if the drain is already done (drain_done_==true), return ok.
//   2. Signal the drain thread to stop (stop_flag_ = true).
//   3. Wait on drain_done_cv_ for up to drain_timeout.
//   4a. On timeout: bump timeout_drop_count_ (NOT drop_count_) and return
//       unexpected(log_drain_timeout).
//   4b. On success: join the thread (it may already be done by then).
//
// IMPORTANT: drain_done_ is set INSIDE the drain thread just before return.
// The join() in 4b is still needed to reclaim the OS thread handle, but it
// should be nearly instantaneous since drain_done_ is only set after the
// thread is about to exit.
fixpp::core::expected_t<void> Logger::shutdown(
    std::chrono::milliseconds drain_timeout)
{
    // Step 1: idempotent guard — if the drain already signalled done, just join.
    {
        std::unique_lock<std::mutex> lock(impl_->drain_done_mutex_);
        if (impl_->drain_done_) {
            lock.unlock();
            if (impl_->drain_thread_.joinable()) {
                impl_->drain_thread_.join();
            }
            return {};
        }
    }

    // Step 2: signal the drain thread to stop.
    impl_->stop_flag_.store(true, std::memory_order_release);

    // Step 3: wait with deadline.
    {
        std::unique_lock<std::mutex> lock(impl_->drain_done_mutex_);
        bool signalled = impl_->drain_done_cv_.wait_for(
            lock, drain_timeout, [this] { return impl_->drain_done_; });

        if (!signalled) {
            // Timeout: bump timeout_drop_count_ (SEPARATE from drop_count_).
            // [contracts/log-core.md §Runtime obligations: three separate counters]
            impl_->timeout_drop_count_.fetch_add(1, std::memory_order_relaxed);
            return std::unexpected(fixpp::core::error::log_drain_timeout);
        }
    }

    // Step 4b: join the thread (near-instant since drain_done_ is set just before return).
    if (impl_->drain_thread_.joinable()) {
        impl_->drain_thread_.join();
    }
    return {};
}

// async_flush() — T027.
// Enqueues a flush sentinel into the ring. The drain thread, on consuming it,
// invokes on_done() (off-hot-path; one allocation for the std::function).
// [contracts/log-core.md New-4: excluded from FR-001 zero-alloc gate]
void Logger::async_flush(std::function<void()> on_done)
{
    if (!on_done) return;  // no-op for null completion

    // Register the completion in the queue BEFORE enqueuing the sentinel, so
    // the drain sees the queue entry before it processes the sentinel slot.
    {
        std::lock_guard<std::mutex> lock(impl_->flush_mutex_);
        impl_->flush_queue_.push(std::move(on_done));
    }

    // Enqueue a sentinel record into the ring (drop_newest if ring is full).
    // The sentinel is a zero Record with flags == k_flush_sentinel_flag.
    // We encode it as a direct enqueue via the Impl's ring protocol.
    //
    // Note: if the sentinel is dropped (overflow), the completion will never
    // fire. Callers should ensure the ring is not perpetually full when calling
    // async_flush(). This is acceptable: async_flush is off-hot-path and
    // callers that care about delivery use shutdown() instead.
    for (;;) {
        std::uint64_t w = impl_->write_sequence_.load(std::memory_order_relaxed);
        std::uint64_t r = impl_->read_sequence_.load(std::memory_order_relaxed);
        if (w - r >= impl_->capacity_) {
            // Ring full: drop the sentinel (pop the completion we just pushed).
            std::lock_guard<std::mutex> lock(impl_->flush_mutex_);
            if (!impl_->flush_queue_.empty()) {
                // Pop ours back out. If another thread pushed too, we might
                // pop the wrong one — but async_flush is documented as
                // single-caller-at-a-time for simplicity.
                impl_->flush_queue_.pop();
            }
            return;  // sentinel dropped; completion will not fire
        }

        if (!impl_->write_sequence_.compare_exchange_weak(
                w, w + 1,
                std::memory_order_acq_rel,
                std::memory_order_relaxed)) {
            continue;
        }

        // CAS success — write the sentinel record.
        RingSlot& slot = impl_->ring_[w % impl_->capacity_];
        Record& rec = slot.record;
        // Zero-initialise the record, then set the sentinel flag.
        rec = Record{};
        rec.flags = k_flush_sentinel_flag;

        slot.sequence.store(w + 1, std::memory_order_release);
        return;
    }
}

}  // namespace fixpp::log
