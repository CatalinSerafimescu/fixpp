// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/_fixtures_/store_temp_dir.hpp
//
// unique_store_dir — per-test temporary directory helper for 008-message-store tests.
//
// Each call returns a unique path under the system's temp directory,
// identified by tag + PID + a per-process atomic counter.  Directories are
// created on construction; cleanup is the caller's responsibility (typically
// std::filesystem::remove_all at end of test).
//
// Usage:
//   auto dir = unique_store_dir("crash_survival");
//   // ... test body ...
//   std::filesystem::remove_all(dir);
#pragma once

#ifdef _WIN32
#include <process.h>  // _getpid()
#else
#include <unistd.h>  // getpid()
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>

namespace fixpp::store_test {

// Portable current-process id (for cross-process temp-dir uniqueness under
// parallel ctest): getpid() on POSIX, _getpid() on Windows.
inline unsigned current_pid() noexcept {
#ifdef _WIN32
    return static_cast<unsigned>(::_getpid());
#else
    return static_cast<unsigned>(::getpid());
#endif
}

/// Returns a new temporary directory path: /tmp/fixpp_test_<tag>_<pid>_<N>/
/// Creates the directory; caller must remove it.
inline std::filesystem::path unique_store_dir(std::string_view tag) {
    static std::atomic<unsigned> ctr{0};
    const auto seq = ctr.fetch_add(1, std::memory_order_relaxed);
    auto p = std::filesystem::temp_directory_path() /
             (std::string("fixpp_test_") + std::string(tag) + "_" +
              std::to_string(current_pid()) + "_" + std::to_string(seq));
    std::filesystem::create_directories(p);
    return p;
}

/// Robustly remove a store temp directory.
///
/// POSIX allows removing a directory whose files are still open (unlink-while-
/// open), so plain remove_all suffices. Windows does NOT: a live FileStore log
/// handle blocks removal outright unless opened FILE_SHARE_DELETE, and even
/// with it the OS keeps a delete-pending directory entry until the LAST handle
/// closes — so removal can transiently fail right after the owning FileStore is
/// destroyed.
///
/// CONTRACT: callers MUST destroy/close every FileStore over `p` before calling
/// this (scope the store, or reset its owning pointer). This helper only
/// absorbs the brief post-close delete-pending lag — it cannot remove a dir
/// whose store is still alive. Retries on Windows, then a final throwing
/// attempt surfaces a clear error if something is genuinely still holding it.
inline void remove_store_dir(const std::filesystem::path& p) {
#ifdef _WIN32
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        if (!ec && !std::filesystem::exists(p)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#endif
    std::filesystem::remove_all(p);  // POSIX: one shot; Windows: final (throwing) attempt
}

}  // namespace fixpp::store_test
