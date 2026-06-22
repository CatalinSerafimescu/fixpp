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
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

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

}  // namespace fixpp::store_test
