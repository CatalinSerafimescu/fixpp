// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/temp_dir.hpp
//
// unique_temp_dir — per-test temporary directory helper, shared across suites.
//
// Each call returns a unique path under the system's temp directory, identified
// by tag + PID + a per-process atomic counter. The directory is created; the
// caller removes it with one of the two removal functions below — NOT with a
// bare std::filesystem::remove_all, which is the silent-leak shape #404 exists
// to remove.
//
// Usage:
//   auto dir = unique_temp_dir("crash_survival");
//   // ... test body; every File{Sink,Store} over `dir` destroyed by here ...
//   remove_temp_dir(dir);        // ... or try_remove_temp_dir(dir) in a destructor
#pragma once

#ifdef _WIN32
#include <process.h>  // _getpid()
#else
#include <unistd.h>  // getpid()
#endif

#include <atomic>
#include <filesystem>
#include <system_error>  // std::error_code, used in both branches
#include <string>
#include <string_view>

#ifdef _WIN32
// Used ONLY by the Windows retry backoff below; kept out of every POSIX TU.
#include <chrono>
#include <thread>
#endif

namespace fixpp::test_support {

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
inline std::filesystem::path unique_temp_dir(std::string_view tag) {
    static std::atomic<unsigned> ctr{0};
    const auto seq = ctr.fetch_add(1, std::memory_order_relaxed);
    auto p = std::filesystem::temp_directory_path() /
             (std::string("fixpp_test_") + std::string(tag) + "_" +
              std::to_string(current_pid()) + "_" + std::to_string(seq));
    std::filesystem::create_directories(p);
    return p;
}

/// Remove the directory, NEVER throwing. Returns true iff it is gone.
///
/// ⚠️ USE THIS ONE FROM A DESTRUCTOR. Destructors are implicitly noexcept, so
/// remove_temp_dir()'s throwing final attempt would call std::terminate() rather
/// than report anything — turning a leaked temp directory into a crashed test
/// run. Several RAII `Cleanup` guards (tests/config) are exactly that shape.
///
/// It keeps the retry and drops the throw — the half a destructor cannot use. A
/// caller that can act on the failure should prefer remove_temp_dir().
///
/// ⚠️ THE RETRY IS NOT ALWAYS THE THING THAT FIXES THE LEAK, and an earlier
/// version of this comment said it was. The two mechanisms are load-bearing at
/// DISJOINT sites:
///   - Where the sink is a local already destroyed before removal (the tests/log
///     fixtures), the retry IS the fix: it absorbs the post-close lag.
///   - Where an owner still holds the sink (the tests/config sites, which reset a
///     shared_ptr<Logger> first), the RESET is the fix and the retry is only
///     insurance. Retrying cannot outlast a handle held for the rest of the test
///     — it would just spend the whole budget and then throw.
/// Which one applies is a property of the call site, not of this helper.
///
/// ⚠️ The catch-all is not defensive padding: `noexcept` here is a PROMISE, and
/// the error_code overloads below still throw on allocation failure. Without the
/// catch, this function's own noexcept would terminate on the very path a
/// destructor-safe helper exists to survive.
inline bool try_remove_temp_dir(const std::filesystem::path& p) noexcept {
    try {
#ifdef _WIN32
        // Windows only. A just-closed handle leaves a delete-pending directory
        // entry, so the first removal can fail for a few milliseconds. Backoff is
        // RAMPED because the lag is normally sub-millisecond: a flat 10 ms would
        // charge the common case ten times what it needs. Budget ~0.9 s total.
        int delay_ms = 1;
        for (int attempt = 0; attempt < 50; ++attempt) {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
            if (!ec) return true;
            if (attempt + 1 == 50) break;  // no sleep after the final attempt
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            delay_ms = (delay_ms * 2 > 20) ? 20 : delay_ms * 2;  // cap, not "stop doubling"
        }
        return false;
#else
        // POSIX permits unlink-while-open, so a failure here is a real error
        // (permissions, a non-empty mount) that retrying cannot clear. One
        // attempt, no sleep — the retry budget above is unreachable off Windows.
        std::error_code ec;
        std::filesystem::remove_all(p, ec);
        return !ec;
#endif
    } catch (...) {
        return false;
    }
}

/// Robustly remove a temp directory created by unique_temp_dir().
///
/// POSIX allows removing a directory whose files are still open (unlink-while-
/// open), so a single remove_all suffices. Windows does NOT: a live FileStore or
/// FileSink handle blocks removal outright unless opened FILE_SHARE_DELETE, and
/// even with it the OS keeps a delete-pending directory entry until the LAST
/// handle closes — so removal can transiently fail right after the owning
/// FileStore is destroyed.
///
/// CONTRACT: callers MUST destroy/close every FileStore/FileSink over `p` before
/// calling this (scope the store/sink, or reset its owning pointer). This helper
/// only absorbs the brief post-close delete-pending lag — it cannot remove a dir
/// whose store is still alive. On Windows it retries first; on every platform the
/// last attempt throws, so a genuine holder surfaces instead of leaking.
inline void remove_temp_dir(const std::filesystem::path& p) {
#ifdef _WIN32
    // Absorb the delete-pending lag before spending a throw on it.
    if (try_remove_temp_dir(p)) return;
#endif
    // ⚠️ POSIX reaches this as its ONLY removal attempt, deliberately: it must stay
    // byte-for-byte the behaviour of the single throwing remove_all this helper
    // replaced, so adopting the helper cannot change what a POSIX failure reports.
    //
    // NOT because remove-then-remove is inherently unsafe — an earlier version of
    // this comment said that, and it does not survive its own logic: the Windows
    // branch above does exactly remove-then-remove, up to 50 times, before the
    // throwing attempt below whose message is the entire diagnostic. Windows
    // accepts that trade because there retrying IS the fix; POSIX gains nothing
    // from it and so keeps the original single shot.
    std::filesystem::remove_all(p);
}

}  // namespace fixpp::test_support
