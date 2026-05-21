// SPDX-License-Identifier: AGPL-3.0-or-later
//
// bench/session/bench_file_store.cpp
//
// 008-message-store seam 13 — FileStore latency benchmarks (disk-bound rows).
//
// Anchor: plan.md Technical-Context latency-ceilings table / [2e §6.6].
// SC-008 / [const §VIII.2]. CI fails on > 2× regression vs per-host baseline
// (disk-bound floor; per-host hardware-specific).
//
// Rows:
//   BM_FileStore_Store_CommitPerMessage — single store + fdatasync
//   BM_FileStore_Reset                 — atomic-rename + parent-dir fsync
//
// FileStore requires a file_io_executor; we use asio::thread_pool(1).
// Each iteration uses a fresh temporary directory to avoid cross-iteration state.
#include <benchmark/benchmark.h>

#include <algorithm>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <span>
#include <string>
#include <vector>

namespace {

using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::seqnum_min;
using fixpp::session::seqnum_t;

// Build a synthetic FIX-like frame of the given size.
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

// Run a single-coroutine task on a fresh io_context and wait for it.
template <class Coro>
auto run_coro(Coro coro) {
    asio::io_context ioc;
    auto fut = asio::co_spawn(ioc.get_executor(), std::move(coro), asio::use_future);
    ioc.run();
    return fut.get();
}

// Generate a unique temporary directory path suffix.
inline std::string unique_suffix() {
    return std::to_string(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count() & 0xFFFF'FFFF));
}

// Cleanup guard for temporary directories.
struct TmpGuard {
    std::filesystem::path p;
    ~TmpGuard() noexcept {
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
    }
};

// ── BM_FileStore_Store_CommitPerMessage ───────────────────────────────────────
// Single store() with commit_per_message policy (fdatasync per record).
// This is the highest-durability / highest-latency policy.
// Expected: disk-bound (ms-range on spinning disk; µs on NVMe).
void BM_FileStore_Store_CommitPerMessage(benchmark::State& state) {
    constexpr std::size_t kFrameBytes = 200;
    constexpr std::size_t kMaxMemBytes = 1024ULL * 1024 * 1024;  // 1 GiB

    for (auto _ : state) {
        state.PauseTiming();

        auto tmp_dir =
            std::filesystem::temp_directory_path() / ("fixpp_bench_file_store_" + unique_suffix());
        std::filesystem::create_directories(tmp_dir);
        TmpGuard guard{tmp_dir};

        asio::thread_pool file_io_pool{1};

        FileStore::Config cfg;
        cfg.directory = tmp_dir;
        cfg.file_io_executor = file_io_pool.get_executor();
        cfg.policy = FileStorePolicy{FileStorePolicy::kind::commit_per_message};
        cfg.max_frame_bytes = 256 * 1024;
        cfg.store_resource = std::pmr::new_delete_resource();

        FileStoreFactory factory{std::move(cfg)};
        auto store_or =
            factory.make("BENCHA", "BENCHB", nullptr, kMaxMemBytes, file_io_pool.get_executor());
        if (!store_or) {
            state.SkipWithError("FileStoreFactory::make failed");
            return;
        }

        auto frame = make_frame(seqnum_min, kFrameBytes);
        benchmark::DoNotOptimize(frame.data());

        state.ResumeTiming();

        run_coro([&]() -> asio::awaitable<void> {
            auto result = co_await store_or.value()->store(
                seqnum_min, std::span<const std::byte>(frame), direction_t::outbound);
            benchmark::DoNotOptimize(result.has_value());
        });

        state.PauseTiming();
        file_io_pool.join();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) *
                            static_cast<int64_t>(kFrameBytes));
}
BENCHMARK(BM_FileStore_Store_CommitPerMessage)->UseRealTime();

// ── BM_FileStore_Reset ────────────────────────────────────────────────────────
// reset() — atomic-rename + parent-dir fsync.
// Expected: disk-bound (varies widely by hardware).
void BM_FileStore_Reset(benchmark::State& state) {
    constexpr std::size_t kMaxMemBytes = 1024ULL * 1024 * 1024;  // 1 GiB

    for (auto _ : state) {
        state.PauseTiming();

        auto tmp_dir =
            std::filesystem::temp_directory_path() / ("fixpp_bench_file_reset_" + unique_suffix());
        std::filesystem::create_directories(tmp_dir);
        TmpGuard guard{tmp_dir};

        asio::thread_pool file_io_pool{1};

        FileStore::Config cfg;
        cfg.directory = tmp_dir;
        cfg.file_io_executor = file_io_pool.get_executor();
        cfg.policy = FileStorePolicy{FileStorePolicy::kind::commit_per_message};
        cfg.max_frame_bytes = 256 * 1024;
        cfg.store_resource = std::pmr::new_delete_resource();

        FileStoreFactory factory{std::move(cfg)};
        auto store_or =
            factory.make("BENCHA", "BENCHB", nullptr, kMaxMemBytes, file_io_pool.get_executor());
        if (!store_or) {
            state.SkipWithError("FileStoreFactory::make failed");
            return;
        }

        // Pre-store one frame to have a non-empty log.
        auto frame = make_frame(seqnum_min, 200);
        run_coro([&]() -> asio::awaitable<void> {
            co_await store_or.value()->store(seqnum_min, std::span<const std::byte>(frame),
                                             direction_t::outbound);
        });

        // Measure only reset().
        state.ResumeTiming();

        run_coro([&]() -> asio::awaitable<void> {
            auto result = co_await store_or.value()->reset();
            benchmark::DoNotOptimize(result.has_value());
        });

        state.PauseTiming();
        file_io_pool.join();
        state.ResumeTiming();
    }
}
BENCHMARK(BM_FileStore_Reset)->UseRealTime();

}  // namespace

BENCHMARK_MAIN();
