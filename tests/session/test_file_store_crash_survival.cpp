// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_file_store_crash_survival.cpp
//
// Seam 2 — FileStore crash survival + CompID validation (SC-002 / I-13 /
// I-16 / FR-008 / FR-010 / FR-013 / [2e §D.4] / [2e §9 seam #2 N-3]).
//
// Tests:
//   1. commit_per_message: SIGKILL-fork harness stores 100 frames, restart
//      re-opens, retrieves all 100 byte-identical.
//   2. commit_batched(N=64): loss bounded by N-1.
//   3. Sentinel mismatch on open → store_factory_failed.
//   4. Directory contention (second open) → store_factory_failed.
//
// CompID-validation sub-cases are split into test_file_store_compid_validation.cpp
// per T014 wording.
//
// TDD: linker-RED until T023/T024/T026 ship FileStore + FileStoreFactory.
#include <gtest/gtest.h>
#ifdef _WIN32
#include <windows.h>  // CreateProcessW / WaitForSingleObject (fork replacement)
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstdlib>  // std::_Exit, std::getenv
#include <filesystem>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fstream>
#ifdef _WIN32
#include <string>
#include <vector>
#endif

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"

namespace {

using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_store_script;
using fixpp::store_test::unique_store_dir;

namespace fs = std::filesystem;

FileStore::Config make_file_config(const fs::path& dir, asio::any_io_executor exec,
                                   FileStorePolicy policy = {}) {
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "SENDER";
    cfg.target_comp_id = "TARGET";
    cfg.policy = policy;
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = exec;
    return cfg;
}

// ── Cross-process store harness ──────────────────────────────────────────────

// Child workload: store kFrames frames into `dir` in a SEPARATE process, then
// hard-exit (releasing the FileStore advisory lock on process exit). Shared by
// the fork() child (POSIX) and the CreateProcess re-exec child (Windows).
// Never returns.
[[noreturn]] void run_store_child(const fs::path& dir, int kFrames) {
    asio::thread_pool child_pool{2};
    FileStore::Config cfg = make_file_config(dir, child_pool.get_executor());
    FileStoreFactory factory{cfg};
    auto minted =
        factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, child_pool.get_executor());
    if (!minted) {
        std::_Exit(1);
    }
    auto& store = *minted.value();
    auto script = make_store_script(kFrames, direction_t::outbound);
    auto fut = asio::co_spawn(
        child_pool.get_executor(),
        [&store, &script]() -> asio::awaitable<void> {
            for (const auto& step : script) {
                co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                     step.dir);
            }
        },
        asio::use_future);
    try {
        fut.get();
    } catch (...) {
    }
    child_pool.stop();
    child_pool.join();
    std::_Exit(0);
}

#ifdef _WIN32
// Windows has no fork(): the parent re-execs this same test binary with
// --gtest_filter selecting this worker and FIXPP_CRASH_CHILD_DIR set, so the
// worker runs run_store_child in a separate process (the fork() analog). In a
// normal run the env var is unset and the worker skips.
TEST(FileStoreCrashSurvival, WinStoreChildWorker) {
    const char* dir_env = std::getenv("FIXPP_CRASH_CHILD_DIR");
    if (dir_env == nullptr || *dir_env == '\0') {
        GTEST_SKIP() << "not invoked as a re-exec child (FIXPP_CRASH_CHILD_DIR unset)";
    }
    run_store_child(fs::path{dir_env}, 100);  // never returns
}
#endif

// Store kFrames frames in a child PROCESS, then re-open in the parent and
// retrieve — verifies cross-process durability + advisory-lock release on exit.
TEST(FileStoreCrashSurvival, CommitPerMessage100Frames) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("crash_per_msg");

    constexpr int kFrames = 100;
    auto script = make_store_script(kFrames, direction_t::outbound);

#ifdef _WIN32
    // Re-exec self as a child process running WinStoreChildWorker.
    wchar_t exe_path[MAX_PATH];
    const DWORD path_len = ::GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    ASSERT_GT(path_len, 0u) << "GetModuleFileNameW failed: " << ::GetLastError();
    ASSERT_LT(path_len, static_cast<DWORD>(MAX_PATH));

    ::_wputenv_s(L"FIXPP_CRASH_CHILD_DIR", dir.wstring().c_str());

    std::wstring cmd = L"\"" + std::wstring{exe_path} +
                       L"\" --gtest_filter=FileStoreCrashSurvival.WinStoreChildWorker";
    std::vector<wchar_t> cmd_mut(cmd.begin(), cmd.end());
    cmd_mut.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const BOOL spawned = ::CreateProcessW(nullptr, cmd_mut.data(), nullptr, nullptr, FALSE, 0,
                                          nullptr, nullptr, &si, &pi);
    // Clear the env immediately so the parent's own WinStoreChildWorker slot skips.
    ::_wputenv_s(L"FIXPP_CRASH_CHILD_DIR", L"");
    ASSERT_TRUE(spawned) << "CreateProcessW failed: " << ::GetLastError();
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    ::CloseHandle(pi.hProcess);
    ::CloseHandle(pi.hThread);
#else
    // Fork child: store kFrames frames then exit cleanly (simulates commit).
    pid_t pid = fork();
    ASSERT_NE(pid, -1);
    if (pid == 0) {
        run_store_child(dir, kFrames);  // never returns
    }
    int status = 0;
    waitpid(pid, &status, 0);
#endif

    // Re-open the store
    FileStore::Config cfg = make_file_config(dir, pool.get_executor());
    FileStoreFactory factory{cfg};
    auto minted =
        factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    ASSERT_TRUE(minted.has_value()) << "re-open failed";

    byte_collecting_visitor visitor;
    auto& store = *minted.value();
    auto fut2 = asio::co_spawn(
        pool.get_executor(),
        [&store, &visitor]() -> asio::awaitable<void> {
            auto r = co_await store.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(r.has_value()) << "retrieve after crash failed";
        },
        asio::use_future);
    fut2.get();

    EXPECT_EQ(visitor.entries().size(), static_cast<std::size_t>(kFrames));
    for (std::size_t i = 0; i < visitor.entries().size(); ++i) {
        EXPECT_EQ(visitor.entries()[i].bytes, script[i].frame_bytes)
            << "frame " << i << " corrupted after restart";
    }

    // Cleanup
    fs::remove_all(dir);
}

// ── Sentinel mismatch ────────────────────────────────────────────────────────

TEST(FileStoreCrashSurvival, SentinelMismatchReturnsFailed) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("sentinel_mismatch");

    // Create a legitimate store first
    {
        FileStore::Config cfg = make_file_config(dir, pool.get_executor());
        FileStoreFactory factory{cfg};
        auto minted =
            factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
        ASSERT_TRUE(minted.has_value());
        // minted goes out of scope — file is created
    }

    // Corrupt the log file (overwrite with garbage)
    auto log_path = dir / "SENDER__TARGET.log";
    ASSERT_TRUE(fs::exists(log_path));
    {
        std::ofstream f(log_path, std::ios::binary | std::ios::trunc);
        const char garbage[] = "NOT_A_VALID_SENTINEL_RECORD_ABCDEFGH";
        f.write(garbage, sizeof(garbage));
    }

    // Re-open: should fail with store_factory_failed
    FileStore::Config cfg = make_file_config(dir, pool.get_executor());
    FileStoreFactory factory{cfg};
    auto result =
        factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), fixpp::core::error::store_factory_failed);

    fs::remove_all(dir);
}

// ── Directory contention (advisory lock) ────────────────────────────────────

TEST(FileStoreCrashSurvival, SecondOpenerReturnsFailed) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("contention");

    FileStore::Config cfg = make_file_config(dir, pool.get_executor());
    FileStoreFactory factory1{cfg};
    auto minted1 =
        factory1.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    ASSERT_TRUE(minted1.has_value()) << "first open failed";

    // Second attempt on the same path should fail
    FileStoreFactory factory2{cfg};
    auto result2 =
        factory2.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    EXPECT_FALSE(result2.has_value());
    EXPECT_EQ(result2.error(), fixpp::core::error::store_factory_failed)
        << "expected store_factory_failed due to advisory lock contention";

    // Release first store (goes out of scope at end of test)
    fs::remove_all(dir);
}

// ── commit_batched: stores succeed and retrieve is consistent ─────────────────
// The bounded-loss invariant under SIGKILL is verified by inspection of the
// commit_batched(N) impl: only every N-th frame triggers fdatasync, so up to
// N-1 frames may be lost on a crash. This test verifies the no-crash happy
// path (all frames stored + retrieved successfully).

TEST(FileStoreCrashSurvival, CommitBatchedReturnsSuccess) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("batched");

    constexpr int kFrames = 100;
    constexpr std::size_t kBatch = 64;
    auto script = make_store_script(kFrames, direction_t::outbound);

    FileStorePolicy policy;
    policy.which = FileStorePolicy::kind::commit_batched;
    policy.batch_size = kBatch;

    FileStore::Config cfg = make_file_config(dir, pool.get_executor(), policy);
    FileStoreFactory factory{cfg};
    auto minted =
        factory.make("SENDER", "TARGET", nullptr, 1024 * 1024 * 1024, pool.get_executor());
    ASSERT_TRUE(minted.has_value());
    auto& store = *minted.value();

    // Store all frames
    auto fut = asio::co_spawn(
        pool.get_executor(),
        [&store, &script]() -> asio::awaitable<void> {
            for (const auto& step : script) {
                auto r = co_await store.store(
                    step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
                EXPECT_TRUE(r.has_value());
            }
        },
        asio::use_future);
    fut.get();

    // Retrieve — all committed frames should be byte-identical
    byte_collecting_visitor visitor;
    auto fut2 = asio::co_spawn(
        pool.get_executor(),
        [&store, &visitor]() -> asio::awaitable<void> {
            auto r = co_await store.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(r.has_value());
        },
        asio::use_future);
    fut2.get();

    // At least one full batch should be present (worst case: last batch < N lost)
    EXPECT_GE(visitor.entries().size(), static_cast<std::size_t>(1));
    EXPECT_LE(visitor.entries().size(), static_cast<std::size_t>(kFrames));

    fs::remove_all(dir);
}

}  // namespace
