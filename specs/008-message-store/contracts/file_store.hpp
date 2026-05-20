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
// Per-instance file_io_executor for pwrite/fdatasync work (FR-024 / I-13 /
// research D-7). flush_for_session_close() is engine-internal, dispatched
// via has_flush_for_session_close concept (I-17 / FR-028).
#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory_resource>
#include <string_view>
#include <variant>

#include <fixpp/session/message_store.hpp>

namespace fixpp::session {

// FileStorePolicy variants. Per-policy data-loss window documented per
// FR-011: commit_per_message = 0% loss; commit_batched(N) = half-batch
// window; commit_interval(ms) = ms-bounded window.
struct commit_per_message_t {};
inline constexpr commit_per_message_t commit_per_message{};

struct commit_batched   { std::size_t every_n_records; };
struct commit_interval  { std::chrono::milliseconds period; };

using FileStorePolicy = std::variant<commit_per_message_t,
                                     commit_batched,
                                     commit_interval>;

class FileStore final : public MessageStore {
public:
    struct Config {
        std::filesystem::path       dir;
        FileStorePolicy             policy           = commit_per_message;
        std::size_t                 max_frame_bytes  = 0;
        std::pmr::memory_resource*  store_resource   = nullptr;
    };

    FileStore(Config cfg,
              std::string_view sender,
              std::string_view target) noexcept;
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
