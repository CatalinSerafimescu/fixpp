// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/file_store.hpp
//
// fixpp::session::FileStore — single append-only log MessageStore implementation.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.3 / §6.3 / §6.3.5. Entity E4 / E10.
// FR-008 (on-disk log) / FR-009 (sentinel + CRC32) / FR-028 (flush hook) /
// I-12 (on-disk record layout) / I-13 (cancellable_dispatch) / I-14 (torn-write).
//
// Single append-only log per session (<sender>__<target>.log) with:
//   - 16-byte per-record header: kind(1) | dir(1) | reserved(2) | seq(4) |
//                                 len(4) | crc32(4)
//   - Payload + 8-byte alignment padding
//   - CRC32 (Castagnoli 0x1EDC6F41) over kind+dir+reserved+seq+len+bytes
//   - Sentinel record: magic(8) | version(1) | session_triple_hash(4) | crc32(4)
//   - Restart algorithm: sentinel verify → per-record CRC32 scan → torn-tail
//     ftruncate + fdatasync (Linux) / SetEndOfFile + FlushFileBuffers (Windows)
//
// FILESYSTEM-SAFETY VALIDATION ([2e §D.4] — Gap 1 close): CompID validation
// is performed in FileStoreFactory::make() BEFORE composing the filename
// and BEFORE opening any file or taking the advisory lock.
//
// SCOPE RESTRICTION ([2e §D.5] — Gap 2 close): FileStore is supported only on
// filesystems where flock / LockFileEx provides effective cross-process
// exclusive-lock semantics. NFS / SMB / FUSE network FS / cluster FS are
// unsupported. make() does NOT detect or warn on such deployments (probe-is-
// worse-than-nothing; operators MUST verify out of band). See [2e §D.5].
//
// Writer-mutex wiring (async_mutex) lands in US3 T041.
// flush_for_session_close() body lands in US2 T032.
// reset() atomic-rename + parent-dir fsync lands in US3 T042.
//
// Mirror of specs/008-message-store/contracts/file_store.hpp (shape oracle).
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>

namespace fixpp::session {

// FileStorePolicy — struct (NOT std::variant) per design-doc §4.3 lines 507–537.
// Per-policy data-loss window documented per FR-011:
//   commit_per_message = 0% loss (fdatasync per record);
//   commit_batched(N)  = up to N-1 record loss window since last batch boundary;
//   commit_interval(ms)= ms-bounded loss window.
struct FileStorePolicy {
    enum class
#ifdef __clang__
        __attribute__((enum_extensibility(closed)))
#endif
        kind : std::uint8_t {
            commit_per_message = 0,
            commit_batched = 1,
            commit_interval = 2,
        };
    kind which = kind::commit_per_message;
    std::size_t batch_size = 1;                                           // commit_batched only
    std::chrono::milliseconds interval = std::chrono::milliseconds{100};  // commit_interval only
};

// Forward declaration of implementation detail
struct FileStoreImpl;

class FileStore final : public MessageStore {
public:
    struct Config {
        // Directory holding session-local store files. Created if missing.
        std::filesystem::path directory;

        // Session identity; encoded into filename so two sessions in the
        // same directory don't collide (sender__target.log — single log
        // per session per §6.3.1).
        //
        // FILESYSTEM-SAFETY VALIDATION (v0.5 per [2e §D.4] — Gap 1 close).
        // FileStoreFactory::make() MUST validate these values before
        // composing the filename and before opening any file or taking
        // the advisory lock: each MUST be non-empty, MUST NOT contain a
        // path separator ('/' on Linux; '/' or '\\' on Windows), a NUL
        // byte, a `.` or `..` path segment, a control character in
        // [0x00, 0x1F] or 0x7F, and the composed path component MUST
        // NOT exceed NAME_MAX (pathconf(_PC_NAME_MAX) on Linux;
        // MAX_PATH minus directory prefix on Windows). On violation,
        // make() returns store_factory_failed before any file is opened.
        // Validation uses primitive string_view::find_first_of / find;
        // std::filesystem::path constructors are NOT invoked until
        // validation passes (preserves noexcept on make()).
        std::string sender_comp_id{};
        std::string target_comp_id{};

        // Durability knob.
        FileStorePolicy policy = {};

        // Maximum frame size accepted on store(). Per-record cap; the log
        // file itself has no size limit other than fs free space.
        std::size_t max_frame_bytes = std::size_t{256} * 1024;

        // Executor for the file-I/O work (§4.3.2).
        // REQUIRED at construction per [2e §4.3.2]:665. FileStoreFactory::make()
        // resolves this with Config-supplied-wins logic (FR-024 / research D-7).
        asio::any_io_executor file_io_executor{};

        // PMR resource for store-owned scratch.
        std::pmr::memory_resource* store_resource = nullptr;
    };

    // 1-arg constructor per design-doc §4.3 line 570.
    // Passes flush_thunk_for<FileStore>() to the MessageStore base — the A1
    // concept gate selects the typed thunk at compile time (T009).
    explicit FileStore(Config c) noexcept;
    ~FileStore() noexcept override;

    // Rule-of-5: FileStore owns a unique file handle + advisory lock via pimpl;
    // non-copyable, non-movable (copying an open advisory lock / file descriptor
    // is undefined behaviour and violates I-14 / FR-009 durability contract).
    FileStore(const FileStore&) = delete;
    FileStore& operator=(const FileStore&) = delete;
    FileStore(FileStore&&) = delete;
    FileStore& operator=(FileStore&&) = delete;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame [[clang::lifetimebound]],
        direction_t dir) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept override;

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override;

    // Engine-internal graceful-close hook dispatched at compile time via
    // fixpp::session::detail::has_flush_for_session_close (I-17; FR-028;
    // Opus N3-P2-1). Runs under Session::close(graceful) outside phase-1's
    // child timeout; NOT invoked under Session::close(terminal) per
    // Appendix D §D.2. Does NOT surface store_cancelled under graceful close.
    //
    // engine-internal: do not call directly — dispatched by the engine's
    // Session-close sequencer per FR-028.
    //
    // Body is stubbed in T023/US1 and implemented in T032/US2.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> flush_for_session_close() noexcept;

    // Internal open: called by FileStoreFactory::make() after CompID validation
    // and executor resolution. Opens the log file, takes the advisory lock, and
    // runs the restart algorithm. Returns true on success; false → store_factory_failed.
    // Declared public to allow the factory TU to call it without friend gymnastics;
    // the doc-only-internal convention (engine-internal, not for external callers)
    // applies per plan.md line 88 alternative to friending.
    // engine-internal: do not call directly.
    [[nodiscard]] bool open_log(const std::string& log_path) noexcept;

private:
    // Pimpl idiom: FileStoreImpl holds the OS file handle, index,
    // buffer state, advisory lock, and restart-scan state. This keeps
    // the header free of OS-specific headers (#include <unistd.h> etc.)
    // that would leak into every TU including file_store.hpp.
    std::unique_ptr<FileStoreImpl> impl_;
};

// Static asserts for FileStorePolicy::kind closed-enum guard
static_assert(static_cast<std::uint8_t>(FileStorePolicy::kind::commit_per_message) == 0);
static_assert(static_cast<std::uint8_t>(FileStorePolicy::kind::commit_batched) == 1);
static_assert(static_cast<std::uint8_t>(FileStorePolicy::kind::commit_interval) == 2);

}  // namespace fixpp::session
