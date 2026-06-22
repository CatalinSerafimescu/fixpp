// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/log/file_sink.hpp
//
// FileSink — rotating append-only file sink (FR-008/FR-009, LOG-002).
//
// Anchors:
//   [2k §4.5]          — FileSink config + rotation semantics
//   contracts/log-sinks.md §FileSink
//   data-model.md §FileSinkConfig
//   [arch §2.3]        — log → {core} only
//   [const §XIV.2]     — Sink has exactly 4 pure-virtual (inherited; 0 added here)
//
// Rotation invariant (from contracts/log-sinks.md):
//   max_keep_count counts archived files only; the live file is additional.
//   Disk bound: max_file_bytes × max_keep_count (archived) + 1 live file
//   that may transiently overshoot max_file_bytes by at most one record before
//   the `>`-triggered rotation.
//
// fsync obligation (FR-005/008):
//   flush(deadline) calls ::fdatasync(fd) on the drain thread within the
//   deadline. Producers NEVER block on I/O.
//
// fsync_fn seam (TS-5 testability):
//   FileSinkConfig::fsync_fn is a function-pointer hook (default = nullptr,
//   which causes FileSink to call the platform fdatasync). Tests inject a mock
//   to assert the call happens on the drain thread without touching I/O.
//
// Portability:
//   FileSink is cross-platform. File I/O goes through std::FILE* (std::fopen/
//   std::fwrite/std::fseek); the durability primitive is a thin platform shim —
//   ::fdatasync(fileno) on POSIX, _commit(_fileno) on Windows (see file_sink.cpp).
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fixpp/log/sink.hpp>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace fixpp::log {

// ── FileSinkConfig ────────────────────────────────────────────────────────────

struct FileSinkConfig : SinkConfig {
    // Directory in which log files are created.
    // Must exist before open() is called; open() does NOT create it.
    std::filesystem::path directory{"."};

    // Base name for log files.
    // Live file:     <base_name>.log
    // Archived file: <base_name>.<iso8601>.log
    std::string base_name{"fixpp"};

    // Maximum bytes written to the live file before rotation is triggered.
    // Rotation happens when bytes_written() > max_file_bytes (strictly greater).
    // Default: 256 MiB.
    std::uint64_t max_file_bytes{256U * 1024U * 1024U};

    // Maximum number of ARCHIVED (rotated) files to keep.
    // When the count of archived files exceeds this limit, the oldest is deleted.
    // The live file is NOT counted toward this limit.
    // Default: 8.
    std::uint32_t max_keep_count{8U};

    // Whether to call fdatasync after flush().
    // When true (default), flush(deadline) calls fdatasync on the open fd.
    bool async_fsync{true};

    // ── Test seam: inject a custom fdatasync implementation.
    //
    // If non-null, flush() calls this function instead of ::fdatasync(fd).
    // Signature: int(int fd) — matches ::fdatasync.
    //
    // Tests set this to a mock that captures the calling thread id, allowing
    // TS-5 to assert the call happens on the drain thread (not the producer).
    std::function<int(int)> fsync_fn{nullptr};
};

// ── FileSink ─────────────────────────────────────────────────────────────────

class FileSink final : public Sink {
public:
    explicit FileSink(FileSinkConfig config);
    ~FileSink() override;

    // Non-copyable, non-movable.
    FileSink(FileSink const&) = delete;
    FileSink& operator=(FileSink const&) = delete;
    FileSink(FileSink&&) = delete;
    FileSink& operator=(FileSink&&) = delete;

    // ── Sink interface ────────────────────────────────────────────────────────

    // open() — opens (creates) the live log file in config_.directory.
    // Returns log_sink_open_failed on failure.
    // Called once by the drain thread before any emit() call.
    [[nodiscard]] fixpp::core::expected_t<void> open() override;

    // emit() — appends a formatted log line to the live file; rotates if
    // bytes_written() > max_file_bytes after the write.
    // Called on the drain thread only. Must not throw.
    void emit(Record const& rec) noexcept override;

    // flush() — calls fdatasync(fd) (or the injected fsync_fn) on the drain
    // thread within the given deadline. Must not throw.
    void flush(std::chrono::milliseconds deadline) noexcept override;

    // close() — flushes and closes the live file. Must not throw.
    void close() noexcept override;

    // ── Accessors ─────────────────────────────────────────────────────────────

    // Path to the currently open live log file.
    // Valid only after a successful open() and before close().
    [[nodiscard]] std::filesystem::path const& current_path() const noexcept;

    // Total bytes written to the current live file since it was opened/rotated.
    [[nodiscard]] std::uint64_t bytes_written() const noexcept;

    // Number of rotations completed since open().
    [[nodiscard]] std::uint64_t rotation_count() const noexcept;

private:
    // Execute a rotation: rename live → archived, open fresh live, prune oldest.
    void rotate() noexcept;

    // Scan config_.directory for archived files matching base_name pattern.
    // Returns them sorted oldest-first.
    [[nodiscard]] std::vector<std::filesystem::path> list_archived() const noexcept;

    // Start/stop the owned fsync worker thread (called from open/close).
    void start_worker() noexcept;
    void stop_worker() noexcept;

    FileSinkConfig config_;
    int fd_{-1};                       // OS fd for the live file (fileno/_fileno)
    std::FILE* stream_{nullptr};       // buffered wrapper (for fprintf)
    std::filesystem::path live_path_;  // <dir>/<base_name>.log
    std::uint64_t bytes_written_{0};
    std::uint64_t rotation_count_{0};

    // ── Owned fsync worker (async_fsync escape — [2k §4.5]) ──────────────────
    //
    // A SINGLE persistent worker thread per FileSink is started in open() and
    // joined (before fclose) in close(). flush(deadline) posts a request to it
    // and wait_for(deadline) on completion: at most one in-flight fsync at a
    // time, zero thread growth on repeated timeouts. close() always joins
    // before the underlying fd is closed — no fd-reuse race.
    enum class WorkerCmd : uint8_t { idle, fsync_requested, stop };

    std::mutex              worker_mu_;
    std::condition_variable worker_cv_;       // drain→worker: new cmd or stop
    std::condition_variable worker_done_cv_;  // worker→drain: fsync complete
    WorkerCmd               worker_cmd_{WorkerCmd::idle};
    bool                    worker_fsync_done_{false};
    std::thread             fsync_worker_;
};

// ── FileSinkFactory ────────────────────────────────────────────────────────────

struct FileSinkFactory final : SinkFactory {
    [[nodiscard]] std::unique_ptr<Sink> make(std::pmr::memory_resource* /*resource*/,
                                             SinkConfig const& config) override {
        return std::make_unique<FileSink>(static_cast<FileSinkConfig const&>(config));
    }
};

}  // namespace fixpp::log
