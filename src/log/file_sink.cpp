// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/log/file_sink.cpp
//
// FileSink implementation — rotating append-only file sink (T028).
//
// Anchors:
//   [2k §4.5]          — FileSink config + rotation semantics
//   contracts/log-sinks.md §FileSink
//   FR-008/FR-009       — file rotation bound + fdatasync obligation
//   [arch §2.3]        — log → {core} only

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <fixpp/core/error.hpp>
#include <fixpp/log/file_sink.hpp>
#include <fixpp/log/format_registry.hpp>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fixpp::log {

namespace {

// Format a Record into a single line string for writing to the file.
// Delegates to the drain-side format_record() from format_registry.hpp.
std::string format_line(Record const& rec) {
    // Timestamp as seconds.microseconds since epoch
    auto ts_ns = rec.timestamp.time_since_epoch().count();
    auto ts_s = ts_ns / 1'000'000'000LL;
    auto ts_us = (ts_ns % 1'000'000'000LL) / 1000LL;

    std::string_view level_str = to_string(rec.level);

    // Resolve and format body using the drain-side registry.
    std::string body;
    try {
        body = detail::format_record(rec);
    } catch (...) {
        body = "[format error]";
    }

    // Build the full line: <timestamp_s>.<us> [LEVEL] cat=<cat> <body>
    char line[1024];
    int n = std::snprintf(line, sizeof(line), "%lld.%06lld [%.*s] cat=%u %s\n", ts_s, ts_us,
                          static_cast<int>(level_str.size()), level_str.data(),
                          static_cast<unsigned>(rec.category), body.c_str());
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof(line)) {
        return "[line too long]\n";
    }
    return {line, static_cast<std::size_t>(n)};
}

// Build the ISO-8601-style timestamp suffix used in archived file names.
// Format: YYYYMMDDTHHMMSS (UTC), e.g. "20260602T153045"
std::string make_iso8601_suffix() noexcept {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%S", &tm_buf);
    return buf;
}

}  // namespace

// ── FileSink ──────────────────────────────────────────────────────────────────

FileSink::FileSink(FileSinkConfig config) : config_{std::move(config)} {
    live_path_ = config_.directory / (config_.base_name + ".log");
}

FileSink::~FileSink() { close(); }

fixpp::core::expected_t<void> FileSink::open() {
    // Open (or create) the live log file for append.
    fd_ = ::open(live_path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        return std::unexpected(fixpp::core::error::log_sink_open_failed);
    }

    // Wrap in a buffered stream for efficient fprintf.
    stream_ = ::fdopen(fd_, "a");
    if (!stream_) {
        ::close(fd_);
        fd_ = -1;
        return std::unexpected(fixpp::core::error::log_sink_open_failed);
    }

    // Seed bytes_written_ with the current file size (we may be appending to
    // an existing file on restart).
    struct stat st{};
    if (::fstat(fd_, &st) == 0) {
        bytes_written_ = static_cast<std::uint64_t>(st.st_size);
    }

    // Start the owned fsync worker (async_fsync escape — [2k §4.5]).
    if (config_.async_fsync) {
        start_worker();
    }

    return {};
}

void FileSink::emit(Record const& rec) noexcept {
    if (stream_ == nullptr) return;

    try {
        std::string line = format_line(rec);
        auto written = std::fwrite(line.data(), 1, line.size(), stream_);
        if (written > 0) {
            bytes_written_ += written;
        }
        // Rotation check: rotate when bytes_written_ > max_file_bytes.
        // The check is AFTER writing (the contract allows one record to overshoot).
        if (bytes_written_ > config_.max_file_bytes) {
            rotate();
        }
    } catch (...) {
        // emit() must never propagate exceptions (drain wraps in try/catch).
    }
}

void FileSink::flush(std::chrono::milliseconds deadline) noexcept {
    if (stream_ == nullptr) return;

    // Flush the stdio buffer first.
    std::fflush(stream_);

    if (!config_.async_fsync) return;

    // Deadline-bounded fsync: [2k §4.5] / contracts/log-sinks.md §FileSink.
    //
    // Dispatch an fsync request to the OWNED persistent worker thread and
    // wait at most `deadline` for completion. On timeout, flush() returns
    // bounded while the worker continues (bounded to ≤1 in-flight fsync —
    // no per-flush thread growth). The worker is joined in close() BEFORE
    // fclose(), so it never operates on a closed/reused fd.
    //
    // Called on the drain thread only (sole caller per Sink contract).
    if (!fsync_worker_.joinable()) return;  // worker not started (open not called)

    try {
        std::unique_lock<std::mutex> lk(worker_mu_);
        worker_fsync_done_ = false;
        worker_cmd_        = WorkerCmd::fsync_requested;
        lk.unlock();
        worker_cv_.notify_one();

        // Wait at most `deadline` for the worker to complete the fsync.
        lk.lock();
        worker_done_cv_.wait_for(lk, deadline, [this] { return worker_fsync_done_; });
        // On timeout: worker continues; next flush() will post another request
        // which the worker processes once the prior fsync completes.
    } catch (...) {
        // Must not propagate (Sink::flush is noexcept).
    }
}

void FileSink::close() noexcept {
    // CRITICAL ordering: stop+join the fsync worker BEFORE closing the fd.
    // The worker reads fd_ directly; joining guarantees no in-flight fsync
    // can land on the fd after fclose()/::close() — no fd-reuse race.
    stop_worker();

    if (stream_ != nullptr) {
        std::fflush(stream_);
        // fclose also closes the underlying fd.
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) — raw FILE* RAII; gsl not adopted
        std::fclose(stream_);
        stream_ = nullptr;
        fd_ = -1;
    } else if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ── Private: fsync worker lifecycle ──────────────────────────────────────────

void FileSink::start_worker() noexcept {
    try {
        // Reset state before spawning.
        {
            std::lock_guard<std::mutex> lk(worker_mu_);
            worker_cmd_         = WorkerCmd::idle;
            worker_fsync_done_  = false;
        }
        fsync_worker_ = std::thread([this]() {
            while (true) {
                std::unique_lock<std::mutex> lk(worker_mu_);
                // Wait until there is a command (fsync_requested or stop).
                worker_cv_.wait(lk, [this] {
                    return worker_cmd_ != WorkerCmd::idle;
                });

                if (worker_cmd_ == WorkerCmd::stop) {
                    return;  // graceful exit
                }

                // Consume the request; release lock before doing I/O.
                worker_cmd_ = WorkerCmd::idle;
                int fd = fd_;
                auto fsync_fn = config_.fsync_fn;  // value copy under lock
                lk.unlock();

                // Run fsync (may block for a long time — that's fine; flush()
                // only wait_for(deadline) and returns early on timeout; we
                // continue here and notify when done regardless).
                if (fd >= 0) {
                    if (fsync_fn) {
                        fsync_fn(fd);
                    } else {
                        ::fdatasync(fd);
                    }
                }

                // Signal completion.
                {
                    std::lock_guard<std::mutex> done_lk(worker_mu_);
                    worker_fsync_done_ = true;
                }
                worker_done_cv_.notify_one();
            }
        });
    } catch (...) {
        // Thread construction failed — worker stays not-joinable; flush() is
        // a no-op (already guarded by joinable() check).
    }
}

void FileSink::stop_worker() noexcept {
    if (!fsync_worker_.joinable()) return;
    try {
        {
            std::lock_guard<std::mutex> lk(worker_mu_);
            worker_cmd_ = WorkerCmd::stop;
        }
        worker_cv_.notify_one();
        fsync_worker_.join();
    } catch (...) {
        // join() is noexcept in practice; std::system_error impossible here
        // unless the thread is not joinable (checked above).
    }
}

std::filesystem::path const& FileSink::current_path() const noexcept { return live_path_; }

std::uint64_t FileSink::bytes_written() const noexcept { return bytes_written_; }

std::uint64_t FileSink::rotation_count() const noexcept { return rotation_count_; }

// ── Private: rotate ───────────────────────────────────────────────────────────

void FileSink::rotate() noexcept {
    try {
        // 0. Stop the fsync worker BEFORE closing the current fd.
        //    rotate() may be called from emit() while a prior timed-out flush()
        //    left the worker running an fsync on the current fd. Joining here
        //    ensures no in-flight fsync can land on the fd after fclose().
        if (config_.async_fsync) {
            stop_worker();
        }

        // 1. Flush and close the current live file.
        if (stream_ != nullptr) {
            std::fflush(stream_);
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory) — raw FILE* RAII; gsl not adopted
            std::fclose(stream_);
            stream_ = nullptr;
            fd_ = -1;
        }

        // 2. Rename live → archived: <base>.<iso8601>.log
        std::string suffix = make_iso8601_suffix();
        auto archived = config_.directory / (config_.base_name + "." + suffix + ".log");

        // If a file with this name already exists (same-second rotation),
        // append a counter suffix.
        if (std::filesystem::exists(archived)) {
            int counter = 1;
            while (true) {
                auto candidate = config_.directory / (config_.base_name + "." + suffix + "_" +
                                                      std::to_string(counter) + ".log");
                if (!std::filesystem::exists(candidate)) {
                    archived = candidate;
                    break;
                }
                ++counter;
            }
        }

        std::error_code ec;
        std::filesystem::rename(live_path_, archived, ec);
        if (ec) {
            // rename failed; attempt to continue without rotation.
            // Re-open the live file.
        }

        // 3. Delete oldest archived files if count > max_keep_count.
        auto archived_list = list_archived();
        while (archived_list.size() > config_.max_keep_count) {
            std::filesystem::remove(archived_list.front(), ec);
            archived_list.erase(archived_list.begin());
        }

        // 4. Open a fresh live file.
        fd_ = ::open(live_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_ >= 0) {
            stream_ = ::fdopen(fd_, "a");
            if (!stream_) {
                ::close(fd_);
                fd_ = -1;
            }
        }

        bytes_written_ = 0;
        ++rotation_count_;

        // Restart the worker on the new fd.
        if (config_.async_fsync && stream_ != nullptr) {
            start_worker();
        }

    } catch (...) {
        // rotate() must not propagate exceptions (called from emit() noexcept).
    }
}

// ── Private: list_archived ────────────────────────────────────────────────────

std::vector<std::filesystem::path> FileSink::list_archived() const noexcept {
    std::vector<std::filesystem::path> result;
    std::error_code ec;

    for (auto const& entry : std::filesystem::directory_iterator(config_.directory, ec)) {
        if (ec) break;
        auto const& p = entry.path();
        // Match: <base_name>.<something>.log  (not the plain live file)
        auto fname = p.filename().string();
        std::string prefix = config_.base_name + ".";
        std::string suffix = ".log";
        if (fname.size() > prefix.size() + suffix.size() && fname.starts_with(prefix) &&
            fname.substr(fname.size() - suffix.size()) == suffix &&
            fname != config_.base_name + ".log") {
            result.push_back(p);
        }
    }

    // Sort lexicographically (ISO-8601 names sort chronologically).
    std::ranges::sort(result);
    return result;
}

}  // namespace fixpp::log
