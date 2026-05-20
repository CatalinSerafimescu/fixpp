// SPDX-License-Identifier: AGPL-3.0-or-later
//
// specs/008-message-store/contracts/file_store.hpp
//
// SHAPE ORACLE — declaration-only contract for fixpp::session::FileStore.
// Anchor: .specify/2e-msgstore.md v0.4 §4.3 / §6.3 / §6.3.5. Catalogue
// S-013 (NEW done at this feature's Gate-B merge).
//
// `final` impl of MessageStore. Single append-only log per session
// (<sender>__<target>.log) with 16-byte per-record header + per-record
// CRC32 (Castagnoli) + sentinel record. Atomic-rename reset() with platform
// durability primitive (Linux: parent-dir fsync MANDATORY; Windows:
// MOVEFILE_WRITE_THROUGH MANDATORY — round-3 C-R3-P1-2; I-15).
// Caller-supplied FileStore::Config::file_io_executor for pwrite/fdatasync
// work (Config field per design-doc §4.3.2 line 665; typical: an
// EngineConfig-exposed 4-thread asio::thread_pool shared across all
// FileStores per design-doc §4.3.2 line 669; FR-024 / I-13 / research D-7).
// flush_for_session_close() is engine-internal, dispatched via
// has_flush_for_session_close concept (I-17 / FR-028).
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <string_view>

#include <asio/any_io_executor.hpp>

#include <fixpp/session/message_store.hpp>

namespace fixpp::session {

// FileStorePolicy — struct (NOT std::variant) per design-doc §4.3 line 507–537.
// Per-policy data-loss window documented per FR-011:
//   commit_per_message      = 0% loss (fdatasync per record);
//   commit_batched(N)       = up to N-1 record loss window since last batch boundary;
//   commit_interval(ms)     = ms-bounded loss window.
struct FileStorePolicy {
    enum class kind : std::uint8_t {
        commit_per_message = 0,
        commit_batched     = 1,
        commit_interval    = 2,
    };
    kind                       which       = kind::commit_per_message;
    std::size_t                batch_size  = 1;                                  // commit_batched only.
    std::chrono::milliseconds  interval    = std::chrono::milliseconds{100};     // commit_interval only.
};

class FileStore final : public MessageStore {
public:
    struct Config {
        // Directory holding session-local store files. Created if missing.
        std::filesystem::path     directory;

        // Session identity; encoded into filename so two sessions in the
        // same directory don't collide (sender__target.log — single log
        // per session per §6.3.1).
        std::string               sender_comp_id;
        std::string               target_comp_id;

        // Durability knob.
        FileStorePolicy           policy            = {};

        // Maximum frame size accepted on store(). Per-record cap; the log
        // file itself has no size limit other than fs free space.
        std::size_t               max_frame_bytes   = 256 * 1024;

        // Executor for the file-I/O work (§4.3.2).
        asio::any_io_executor     file_io_executor;

        // PMR resource for store-owned scratch.
        std::pmr::memory_resource* store_resource   = nullptr;
    };

    // 1-arg constructor per design-doc §4.3 line 570.
    explicit FileStore(Config c) noexcept;
    ~FileStore() override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    store(seqnum_t seq,
          std::span<const std::byte> frame [[clang::lifetimebound]],
          direction_t dir) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    retrieve(seqnum_t begin,
             seqnum_t end,
             direction_t dir,
             retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>>
    next_seqnum(direction_t dir, bool increment) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    reset() noexcept override;

    // Engine-internal, non-virtual, non-public hook dispatched at compile
    // time via fixpp::session::detail::has_flush_for_session_close (I-17;
    // FR-028; Opus N3-P2-1). Runs under Session::close(graceful) outside
    // phase-1's child timeout; NOT invoked under Session::close(terminal)
    // per Appendix D §D.2. Does NOT surface store_cancelled under graceful
    // close. The friend declaration is left to the impl-side header (this
    // is the shape oracle).
    //
    //   [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    //   flush_for_session_close() noexcept;
};

}  // namespace fixpp::session
