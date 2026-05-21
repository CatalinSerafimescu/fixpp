// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/fuzz/fuzz_message_store.cpp
//
// 008-message-store seam 21 — libFuzzer harness for MessageStore.
//
// Anchor: SC-011 / FR-033 / [const §VII.7] voluntary / [const §IX.2].
//
// Drives random interleavings of store / retrieve / reset / next_seqnum
// against BOTH MemoryStore and FileStore using the fuzzer input as a
// script of operations. Each byte of the input is interpreted as a (kind,
// direction, payload_len) triple to select the operation and its arguments.
//
// Properties asserted under -fsanitize=fuzzer,address,undefined:
//   • no UAF / OOB / UB — ASan / UBSan;
//   • no abort / SIGSEGV on any byte sequence;
//   • every awaitable completes within bounded handler budget;
//   • no global-heap allocation from MemoryStore::store() in the bounded
//     capacity path (structural invariant — PMR arena pre-allocated in ctor).
//
// FileStore operates on a temporary directory per fuzzer invocation; the
// directory is cleaned up on exit. We accept occasional I/O errors (ENOSPC,
// store_io_failure) as legal expected_t returns — they do NOT abort the fuzzer.
//
// Build (requires -DFIXPP_BUILD_FUZZ=ON):
//   cmake --preset linux-clang-asan -DFIXPP_BUILD_FUZZ=ON
//   cmake --build --preset linux-clang-asan --target fuzz_message_store
//
// 10-min corpus run:
//   build/linux-clang-asan/bin/fuzz_message_store \
//       tests/fuzz/corpus/message_store/ \
//       -max_total_time=600 -timeout=10 -rss_limit_mb=1024

#include <algorithm>
#include <array>
#include <atomic>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory_resource>
#include <span>
#include <string>

namespace {

using fixpp::session::capacity_policy;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::MemoryStore;
using fixpp::session::retrieve_visitor;
using fixpp::session::seqnum_min;
using fixpp::session::seqnum_t;
using fixpp::session::visit_result;

// ── Null visitor that discards frames ────────────────────────────────────────

struct DiscardVisitor : retrieve_visitor {
    asio::awaitable<fixpp::core::expected_t<visit_result>> on_frame(
        seqnum_t /*seq*/,
        std::span<const std::byte> frame [[clang::lifetimebound]]) noexcept override {
        // Read the first byte to prevent elision (DoNotOptimize-equivalent
        // in a constexpr-free context).
        volatile std::uint8_t sink = frame.empty() ? 0u : static_cast<std::uint8_t>(frame[0]);
        (void)sink;
        co_return visit_result::cont;
    }
};

// ── Script interpretation ────────────────────────────────────────────────────
//
// Each 4-byte chunk of the input encodes one store operation:
//   byte 0 — op:   0=store, 1=retrieve, 2=reset, 3=next_seqnum_read,
//                  4=next_seqnum_incr, else=skip
//   byte 1 — dir:  bit0 → inbound(0)/outbound(1)
//   byte 2 — seq_lo
//   byte 3 — seq_hi  (together: seqnum_t = seq_lo | (seq_hi << 8))
//
// The fuzzer may generate seqnums out of order — that's intentional; the
// store must return store_seqnum_out_of_order, NOT crash.

enum class Op : std::uint8_t {
    store_op = 0,
    retrieve_op = 1,
    reset_op = 2,
    next_seqnum_r = 3,
    next_seqnum_i = 4,
};

constexpr std::size_t kChunkSize = 4;

inline direction_t decode_dir(std::uint8_t b) noexcept {
    return (b & 0x1u) ? direction_t::outbound : direction_t::inbound;
}

inline Op decode_op(std::uint8_t b) noexcept {
    switch (b % 5u) {
        case 0:
            return Op::store_op;
        case 1:
            return Op::retrieve_op;
        case 2:
            return Op::reset_op;
        case 3:
            return Op::next_seqnum_r;
        default:
            return Op::next_seqnum_i;
    }
}

// ── Run a single-coro task on io_context ─────────────────────────────────────

template <class Coro>
static void run_coro(asio::io_context& ioc, Coro coro) {
    auto fut = asio::co_spawn(ioc.get_executor(), std::move(coro), asio::use_future);
    // Run until the coroutine suspends or completes. Use poll() to avoid
    // blocking the fuzzer if a coroutine is never posted-back (safety valve).
    for (int guard = 0; guard < 100'000 && !ioc.stopped(); ++guard) {
        ioc.poll_one();
    }
    ioc.restart();
    // Retrieve result to propagate any exception (assert/abort in DEBUG mode).
    (void)fut.get();
}

// ── MemoryStore fuzzer drive ──────────────────────────────────────────────────

static void fuzz_memory_store(const std::uint8_t* data, std::size_t size) {
    // PMR slab: pre-allocated for bounded capacity.
    // Post-/gate-a slab footprint: each direction stores kCapacity frames of up
    // to kMaxFrameBytes each; plus index-vector reserve headroom (64 KiB).
    constexpr std::size_t kCapacity = 256;
    constexpr std::size_t kMaxFrameBytes = 1024;
    constexpr std::size_t kSlabBytes = (2 * kCapacity * kMaxFrameBytes) + (64 * 1024);
    static thread_local std::array<std::byte, kSlabBytes> slab_buf;
    std::pmr::monotonic_buffer_resource pmr{slab_buf.data(), slab_buf.size(),
                                            std::pmr::null_memory_resource()};

    MemoryStore::Config cfg;
    cfg.policy = capacity_policy::bounded;
    cfg.inbound_capacity = kCapacity;
    cfg.outbound_capacity = kCapacity;
    cfg.max_frame_bytes = 1024;
    cfg.store_resource = &pmr;
    MemoryStore store{cfg};

    asio::io_context ioc;
    std::array<std::byte, 1024> frame_buf;

    for (std::size_t off = 0; off + kChunkSize <= size; off += kChunkSize) {
        const Op op = decode_op(data[off + 0]);
        const direction_t dir = decode_dir(data[off + 1]);
        const seqnum_t seq =
            static_cast<seqnum_t>(static_cast<std::uint32_t>(data[off + 2]) |
                                  (static_cast<std::uint32_t>(data[off + 3]) << 8u));

        switch (op) {
            case Op::store_op: {
                // Frame length: low nibble of seq, 1..64 bytes.
                std::size_t flen = static_cast<std::size_t>((seq & 0x3Fu) + 1u);
                if (flen > frame_buf.size()) flen = frame_buf.size();
                // Fill with a recognisable pattern.
                for (std::size_t i = 0; i < flen; ++i) {
                    frame_buf[i] = static_cast<std::byte>(static_cast<std::uint8_t>(i ^ seq));
                }
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store.store(
                        seq, std::span<const std::byte>(frame_buf.data(), flen), dir);
                    (void)r;  // legal errors (out-of-order, exhausted) are expected
                });
                break;
            }
            case Op::retrieve_op: {
                seqnum_t begin = (seq == 0) ? seqnum_min : seq;
                seqnum_t end = 0;  // open-ended walk
                DiscardVisitor vis;
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store.retrieve(begin, end, dir, vis);
                    (void)r;
                });
                break;
            }
            case Op::reset_op:
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store.reset();
                    (void)r;
                });
                break;
            case Op::next_seqnum_r:
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store.next_seqnum(dir, /*increment=*/false);
                    (void)r;
                });
                break;
            case Op::next_seqnum_i:
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store.next_seqnum(dir, /*increment=*/true);
                    (void)r;
                });
                break;
        }
    }
}

// ── FileStore fuzzer drive ────────────────────────────────────────────────────

static void fuzz_file_store(const std::uint8_t* data, std::size_t size,
                            const std::filesystem::path& tmp_dir, asio::thread_pool& io_pool) {
    constexpr std::size_t kMaxMemBytes = 1024ULL * 1024 * 64;  // 64 MiB guard

    FileStore::Config cfg;
    cfg.directory = tmp_dir;
    cfg.file_io_executor = io_pool.get_executor();
    cfg.policy = FileStorePolicy{FileStorePolicy::kind::commit_per_message};
    cfg.max_frame_bytes = 1024;
    cfg.store_resource = std::pmr::new_delete_resource();

    FileStoreFactory factory{std::move(cfg)};
    auto store_or = factory.make("FUZZ", "TARGET", nullptr, kMaxMemBytes, io_pool.get_executor());
    if (!store_or) return;  // e.g. store_factory_failed — legal; skip this invocation
    auto& store = *store_or;

    asio::io_context ioc;
    std::array<std::byte, 1024> frame_buf;

    for (std::size_t off = 0; off + kChunkSize <= size; off += kChunkSize) {
        const Op op = decode_op(data[off + 0]);
        const direction_t dir = decode_dir(data[off + 1]);
        const seqnum_t seq =
            static_cast<seqnum_t>(static_cast<std::uint32_t>(data[off + 2]) |
                                  (static_cast<std::uint32_t>(data[off + 3]) << 8u));

        switch (op) {
            case Op::store_op: {
                std::size_t flen = static_cast<std::size_t>((seq & 0x3Fu) + 1u);
                if (flen > frame_buf.size()) flen = frame_buf.size();
                for (std::size_t i = 0; i < flen; ++i) {
                    frame_buf[i] = static_cast<std::byte>(static_cast<std::uint8_t>(i ^ seq));
                }
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store->store(
                        seq, std::span<const std::byte>(frame_buf.data(), flen), dir);
                    (void)r;
                });
                break;
            }
            case Op::retrieve_op: {
                seqnum_t begin = (seq == 0) ? seqnum_min : seq;
                DiscardVisitor vis;
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store->retrieve(begin, 0, dir, vis);
                    (void)r;
                });
                break;
            }
            case Op::reset_op:
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store->reset();
                    (void)r;
                });
                break;
            case Op::next_seqnum_r:
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store->next_seqnum(dir, /*increment=*/false);
                    (void)r;
                });
                break;
            case Op::next_seqnum_i:
                run_coro(ioc, [&]() -> asio::awaitable<void> {
                    auto r = co_await store->next_seqnum(dir, /*increment=*/true);
                    (void)r;
                });
                break;
        }
    }
}

}  // namespace

// ── LLVMFuzzerTestOneInput ────────────────────────────────────────────────────

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size == 0) return 0;

    // First byte selects which store variant to fuzz:
    //   bit 0 clear → MemoryStore; bit 0 set → FileStore
    const bool use_file_store = (data[0] & 0x1u) != 0;
    const std::uint8_t* payload = data + 1;
    const std::size_t payload_size = (size > 1) ? (size - 1) : 0;

    if (!use_file_store || payload_size == 0) {
        // MemoryStore: always fuzz even when payload_size == 0 (exercises ctor).
        fuzz_memory_store(payload, payload_size);
    } else {
        // FileStore: needs a unique temp directory per invocation.
        // Use a per-process atomic counter (not the `data` pointer which is
        // stable across invocations on the same libFuzzer thread and collides).
        static std::atomic<unsigned> fuzz_dir_ctr{0};
        const auto fuzz_dir_seq = fuzz_dir_ctr.fetch_add(1, std::memory_order_relaxed);
        const std::filesystem::path tmp_dir =
            std::filesystem::temp_directory_path() /
            ("fixpp_fuzz_fs_" + std::to_string(static_cast<unsigned>(::getpid())) +
             "_" + std::to_string(fuzz_dir_seq));

        std::error_code ec;
        std::filesystem::create_directories(tmp_dir, ec);
        if (ec) return 0;

        // Cleanup on exit from this invocation.
        struct DirGuard {
            const std::filesystem::path& p;
            ~DirGuard() noexcept {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } guard{tmp_dir};

        asio::thread_pool io_pool{1};
        fuzz_file_store(payload, payload_size, tmp_dir, io_pool);
        io_pool.join();
    }

    return 0;
}
