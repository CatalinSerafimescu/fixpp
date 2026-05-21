// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/bench_memory_store.cpp
//
// 008-message-store seam 13 — MemoryStore latency benchmarks.
//
// Anchor: plan.md Technical-Context latency-ceilings table / [2e §6.6].
// SC-008 / [const §VIII.2]. CI fails on > 5% regression vs per-host baseline.
//
// Rows:
//   BM_MemoryStore_Store_200B   — store() 200-byte frame   (single-digit µs)
//   BM_MemoryStore_Store_1KiB   — store() 1 KiB frame       (single-digit µs)
//   BM_MemoryStore_NextSeqnum   — next_seqnum(false)         (sub-µs)
//   BM_MemoryStore_Retrieve100  — retrieve(100 frames)        (µs)
//   BM_MemoryStore_Reset        — reset()                     (µs)
//
// Each row uses a fresh asio::io_context per benchmark iteration so that
// the coroutine executor state is not shared between measurement points.
// benchmark::DoNotOptimize() prevents the compiler from eliding results.
#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace {

using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::MemoryStore;
using fixpp::session::seqnum_min;
using fixpp::session::seqnum_t;

// ── Helpers ───────────────────────────────────────────────────────────────────

// Build a synthetic FIX-like frame of the given size (opaque to the store).
inline std::vector<std::byte> make_frame(seqnum_t seq, std::size_t total_bytes) {
    std::string raw;
    raw +=
        "8=FIX.4.4\x01"
        "35=D\x01"
        "34=";
    raw += std::to_string(static_cast<unsigned>(seq));
    raw +=
        "\x01"
        "49=SENDER\x01"
        "10=123\x01";

    if (raw.size() < total_bytes) {
        raw.append(total_bytes - raw.size(), 'X');
    }

    std::vector<std::byte> result(raw.size());
    std::transform(raw.begin(), raw.end(), result.begin(),
                   [](char c) { return static_cast<std::byte>(c); });
    return result;
}

// Build a MemoryStore in unbounded mode with capacity large enough for the
// bench workload. PMR resource defaults to std::pmr::new_delete_resource().
inline MemoryStore make_store(std::size_t inbound_cap = 10'000, std::size_t outbound_cap = 10'000) {
    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::unbounded;
    cfg.inbound_capacity = inbound_cap;
    cfg.outbound_capacity = outbound_cap;
    cfg.max_frame_bytes = 256 * 1024;
    cfg.store_resource = std::pmr::new_delete_resource();
    return MemoryStore{cfg};
}

// Run a single-coroutine task on a fresh io_context and wait for it.
template <class Coro>
auto run_coro(Coro coro) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc.get_executor(), std::move(coro), asio::use_future);
    ioc.run();
    return fut.get();
}

// ── BM_MemoryStore_Store_200B ─────────────────────────────────────────────────
// Single store() of a 200-byte frame (the typical FIX control-message path).
// Expected: single-digit µs.
// Setup (store construction + frame allocation) hoisted OUT of the timed loop;
// a per-iteration seqnum avoids seq-mismatch errors across iterations.
void BM_MemoryStore_Store_200B(benchmark::State& state) {
    constexpr std::size_t kFrameBytes = 200;

    auto store = make_store();
    auto frame = make_frame(seqnum_min, kFrameBytes);
    benchmark::DoNotOptimize(frame.data());
    seqnum_t seq = seqnum_min;

    for (auto _ : state) {
        run_coro([&]() -> asio::awaitable<void> {
            auto result = co_await store.store(seq, std::span<const std::byte>(frame),
                                               direction_t::outbound);
            benchmark::DoNotOptimize(result.has_value());
        });
        ++seq;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(kFrameBytes));
}
BENCHMARK(BM_MemoryStore_Store_200B);

// ── BM_MemoryStore_Store_1KiB ─────────────────────────────────────────────────
// Single store() of a 1 KiB frame (FIX data messages, filled with application
// payload). Expected: single-digit µs.
// Setup hoisted OUT of the timed loop (same pattern as 200B bench).
void BM_MemoryStore_Store_1KiB(benchmark::State& state) {
    constexpr std::size_t kFrameBytes = 1024;

    auto store = make_store();
    auto frame = make_frame(seqnum_min, kFrameBytes);
    benchmark::DoNotOptimize(frame.data());
    seqnum_t seq = seqnum_min;

    for (auto _ : state) {
        run_coro([&]() -> asio::awaitable<void> {
            auto result = co_await store.store(seq, std::span<const std::byte>(frame),
                                               direction_t::outbound);
            benchmark::DoNotOptimize(result.has_value());
        });
        ++seq;
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(kFrameBytes));
}
BENCHMARK(BM_MemoryStore_Store_1KiB);

// ── BM_MemoryStore_NextSeqnum ─────────────────────────────────────────────────
// next_seqnum(dir, false) — read-only query, no increment.
// Expected: sub-µs (effectively a counter load under mutex, coroutine hop).
void BM_MemoryStore_NextSeqnum(benchmark::State& state) {
    // Pre-populate with one frame so next_seqnum is 2.
    auto store = make_store();
    auto frame = make_frame(seqnum_min, 200);
    run_coro([&]() -> asio::awaitable<void> {
        co_await store.store(seqnum_min, std::span<const std::byte>(frame), direction_t::outbound);
    });

    for (auto _ : state) {
        run_coro([&]() -> asio::awaitable<void> {
            auto result = co_await store.next_seqnum(direction_t::outbound, /*increment=*/false);
            benchmark::DoNotOptimize(result.has_value());
        });
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_MemoryStore_NextSeqnum);

// ── BM_MemoryStore_Retrieve100 ───────────────────────────────────────────────
// retrieve() walking 100 stored frames via a collecting visitor.
// Expected: µs (snapshot copy + visitor loop, no suspension).
struct NullVisitor : fixpp::session::retrieve_visitor {
    std::size_t count = 0;

    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t /*seq*/,
        std::span<const std::byte> frame [[clang::lifetimebound]]) noexcept override {
        benchmark::DoNotOptimize(frame.data());
        ++count;
        co_return fixpp::session::visit_result::cont;
    }
};

void BM_MemoryStore_Retrieve100(benchmark::State& state) {
    constexpr std::size_t kCount = 100;

    // Pre-populate 100 outbound frames.
    auto store = make_store(10'000, 10'000);
    run_coro([&]() -> asio::awaitable<void> {
        for (seqnum_t i = 1; i <= kCount; ++i) {
            auto frame = make_frame(i, 200);
            co_await store.store(i, std::span<const std::byte>(frame), direction_t::outbound);
        }
    });

    for (auto _ : state) {
        NullVisitor vis;
        run_coro([&]() -> asio::awaitable<void> {
            auto result = co_await store.retrieve(1, kCount, direction_t::outbound, vis);
            benchmark::DoNotOptimize(result.has_value());
        });
        benchmark::DoNotOptimize(vis.count);
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(kCount));
}
BENCHMARK(BM_MemoryStore_Retrieve100);

// ── BM_MemoryStore_Reset ─────────────────────────────────────────────────────
// reset() — zero-pass (clear all entries, reset counters).
// Expected: µs (a clear() on the two vectors + counter reset under mutex).
void BM_MemoryStore_Reset(benchmark::State& state) {
    constexpr std::size_t kPrefill = 100;

    for (auto _ : state) {
        state.PauseTiming();
        auto store = make_store();

        // Pre-fill 100 frames so reset() has work to do.
        run_coro([&]() -> asio::awaitable<void> {
            for (seqnum_t i = 1; i <= kPrefill; ++i) {
                auto frame = make_frame(i, 200);
                co_await store.store(i, std::span<const std::byte>(frame), direction_t::outbound);
            }
        });

        // Measure only reset().
        state.ResumeTiming();
        run_coro([&]() -> asio::awaitable<void> {
            auto result = co_await store.reset();
            benchmark::DoNotOptimize(result.has_value());
        });
    }
}
BENCHMARK(BM_MemoryStore_Reset);

}  // namespace

BENCHMARK_MAIN();
