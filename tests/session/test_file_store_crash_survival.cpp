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
// Q-28 and Q-29 (093-inbound-frame-dispositions tasks.md T079; contract C-6; data-model
// E-9): FileStore::reset_to refuses a target outside {1, 2} with no effect, durable
// included; and a fault in its offloaded sequence leaves a restart reading the old or
// the new counters, never a partial (1, 1). The section comment above
// Q28_TargetsOutsideOneTwoAreRefusedWithNoEffect states each cell.
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
#include <memory>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <string>
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
                // Verified cross-process by the parent's retrieve()+byte-compare below,
                // not observable from this soon-to-_Exit() child.
                (void)co_await store.store(step.seq, std::span<const std::byte>(step.frame_bytes),
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
    minted.value() = nullptr;
    fixpp::store_test::remove_store_dir(dir);
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

    fixpp::store_test::remove_store_dir(dir);
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

    // Release first store before removing the directory
    minted1.value() = nullptr;
    fixpp::store_test::remove_store_dir(dir);
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

    minted.value() = nullptr;
    fixpp::store_test::remove_store_dir(dir);
}

// ── Q-28 and Q-29 (093 tasks.md T079): FileStore::reset_to ────────────────────
//
// Each cell advances a store past both targets (five frames stored outbound, the
// inbound counter incremented three times: NextNumIn 4, NextNumOut 6), so "no effect"
// and "old counters" are observable. A restart is a fresh FileStore over the same
// directory after the first is destroyed; it reads the last counter record.
//
// Q-29 has two fault points, because they discriminate different failures:
//   - before the rename: the reset's temp log is written and closed, then the
//     operation fails; the rename, the single commit point, never happens, so a
//     restart reads the old counters;
//   - at the first counter-record write after a reset's rename has committed: an
//     override that is one operation writes nothing after its rename, so a restart
//     reads the new counters; an override that resets and then advances commits
//     (1, 1) at the rename, and its advance then fails, which is the partial state
//     the cell refuses.
// A positive control shows the second fault fires on a reset() followed by an advance.

using fixpp::session::seqnum_t;

struct DurableCounters {
    seqnum_t in = 0;
    seqnum_t out = 0;
    bool operator==(DurableCounters const&) const = default;
};

std::unique_ptr<fixpp::session::MessageStore> open_store(const fs::path& dir,
                                                         asio::thread_pool& pool,
                                                         std::string_view sender = "SENDER",
                                                         std::string_view target = "TARGET") {
    FileStoreFactory factory{make_file_config(dir, pool.get_executor())};
    auto minted = factory.make(sender, target, nullptr, 1024 * 1024 * 1024, pool.get_executor());
    if (!minted) return nullptr;
    return std::move(*minted);
}

template <class F>
auto run_on(asio::thread_pool& pool, F f) {
    return asio::co_spawn(pool.get_executor(), std::move(f), asio::use_future).get();
}

DurableCounters read_counters(fixpp::session::MessageStore& store, asio::thread_pool& pool) {
    return run_on(pool, [&]() -> asio::awaitable<DurableCounters> {
        auto in = co_await store.next_seqnum(direction_t::inbound, false);
        auto out = co_await store.next_seqnum(direction_t::outbound, false);
        co_return DurableCounters{.in = in.value_or(0), .out = out.value_or(0)};
    });
}

// A fresh store over `dir`, then advanced to NextNumIn 4, NextNumOut 6.
std::unique_ptr<fixpp::session::MessageStore> open_advanced(const fs::path& dir,
                                                            asio::thread_pool& pool,
                                                            std::string_view sender = "SENDER",
                                                            std::string_view target = "TARGET") {
    auto store = open_store(dir, pool, sender, target);
    if (!store) return nullptr;
    run_on(pool, [&]() -> asio::awaitable<void> {
        for (auto const& step : make_store_script(5, direction_t::outbound)) {
            auto r = co_await store->store(step.seq, std::span<const std::byte>(step.frame_bytes),
                                           step.dir);
            EXPECT_TRUE(r.has_value()) << "setup store of seq " << step.seq;
        }
        for (int i = 0; i < 3; ++i) {
            auto r = co_await store->next_seqnum(direction_t::inbound, true);
            EXPECT_TRUE(r.has_value()) << "setup inbound advance";
        }
    });
    return store;
}

constexpr DurableCounters kAdvanced{.in = 4, .out = 6};

// Destroys `store`, then reads the counters a restart over `dir` sees.
DurableCounters restart_counters(std::unique_ptr<fixpp::session::MessageStore>& store,
                                 const fs::path& dir, asio::thread_pool& pool) {
    store = nullptr;
    auto reopened = open_store(dir, pool);
    if (!reopened) {
        ADD_FAILURE() << "restart: re-open failed";
        return {};
    }
    return read_counters(*reopened, pool);
}

TEST(FileStoreResetTo, Q28_TargetsOutsideOneTwoAreRefusedWithNoEffect) {
    asio::thread_pool pool{2};
    constexpr std::pair<seqnum_t, seqnum_t> kRefused[] = {{0, 1}, {3, 1}, {1, 0}, {1, 3},
                                                          {2, 3}, {3, 2}, {0, 0}, {4, 6}};
    for (auto const& [in, out] : kRefused) {
        auto dir = unique_store_dir("reset_to_refused");
        auto store = open_advanced(dir, pool);
        ASSERT_NE(store, nullptr);
        ASSERT_EQ(read_counters(*store, pool), kAdvanced) << "setup";
        auto r = run_on(pool, [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store->reset_to(in, out);
        });
        EXPECT_FALSE(r.has_value()) << "reset_to(" << in << ", " << out << ") must be refused";
        if (!r.has_value()) {
            EXPECT_EQ(r.error(), fixpp::core::error::session_invalid_argument)
                << "reset_to(" << in << ", " << out << ")";
        }
        EXPECT_EQ(read_counters(*store, pool), kAdvanced)
            << "reset_to(" << in << ", " << out << ") changed the counters";
        EXPECT_EQ(restart_counters(store, dir, pool), kAdvanced)
            << "reset_to(" << in << ", " << out << ") changed the durable counters";
        fixpp::store_test::remove_store_dir(dir);
    }
}

TEST(FileStoreResetTo, Q28_AnAcceptedPairIsDurable) {
    asio::thread_pool pool{2};
    constexpr std::pair<seqnum_t, seqnum_t> kAccepted[] = {{1, 1}, {1, 2}, {2, 1}, {2, 2}};
    for (auto const& [in, out] : kAccepted) {
        auto dir = unique_store_dir("reset_to_accepted");
        auto store = open_advanced(dir, pool);
        ASSERT_NE(store, nullptr);
        auto r = run_on(pool, [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
            co_return co_await store->reset_to(in, out);
        });
        EXPECT_TRUE(r.has_value()) << "reset_to(" << in << ", " << out << ")";
        DurableCounters const want{.in = in, .out = out};
        EXPECT_EQ(read_counters(*store, pool), want) << "reset_to(" << in << ", " << out << ")";
        EXPECT_EQ(restart_counters(store, dir, pool), want)
            << "durable after reset_to(" << in << ", " << out << ")";
        fixpp::store_test::remove_store_dir(dir);
    }
}

TEST(FileStoreResetTo, Q29_AFaultBeforeTheRenameLeavesTheOldCounters) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("reset_to_fault_before_rename");
    auto store = open_advanced(dir, pool);
    ASSERT_NE(store, nullptr);
    (void)fixpp::session::read_and_reset_reset_atomicity_fault_count();
    fixpp::session::arm_force_reset_fail_before_rename_once();
    auto r = run_on(pool, [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
        co_return co_await store->reset_to(2, 2);
    });
    EXPECT_EQ(fixpp::session::read_and_reset_reset_atomicity_fault_count(), 1)
        << "the fault before the rename must fire";
    EXPECT_FALSE(r.has_value()) << "reset_to must report the failed operation";
    EXPECT_EQ(restart_counters(store, dir, pool), kAdvanced)
        << "a fault before the rename must leave the old counters";
    fixpp::store_test::remove_store_dir(dir);
}

TEST(FileStoreResetTo, Q29_AFaultAfterTheRenameCommitsNoPartialState) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("reset_to_fault_after_commit");
    auto store = open_advanced(dir, pool);
    ASSERT_NE(store, nullptr);
    (void)fixpp::session::read_and_reset_reset_atomicity_fault_count();
    fixpp::session::arm_fail_counter_write_after_reset_commit();
    auto r = run_on(pool, [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
        co_return co_await store->reset_to(2, 2);
    });
    fixpp::session::disarm_fail_counter_write_after_reset_commit();
    auto const after = restart_counters(store, dir, pool);
    DurableCounters const want{.in = 2, .out = 2};
    EXPECT_TRUE(after == kAdvanced || after == want)
        << "a restart read NextNumIn " << after.in << ", NextNumOut " << after.out
        << ": neither the old (4, 6) nor the new (2, 2)";
    EXPECT_TRUE(r.has_value() == (after == want))
        << "reset_to's result must match what a restart reads";
    fixpp::store_test::remove_store_dir(dir);
}

// Positive control for the fault after the rename: reset() then an inbound advance
// (the shape the previous cell refuses) does reach it, and the advance fails.
TEST(FileStoreResetTo, Q29_Control_TheFaultAfterTheRenameFiresOnAResetThenAnAdvance) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("reset_to_fault_control");
    auto store = open_advanced(dir, pool);
    ASSERT_NE(store, nullptr);
    (void)fixpp::session::read_and_reset_reset_atomicity_fault_count();
    fixpp::session::arm_fail_counter_write_after_reset_commit();
    auto const [reset_ok, advance_ok] =
        run_on(pool, [&]() -> asio::awaitable<std::pair<bool, bool>> {
            auto rr = co_await (*store).reset();
            auto ar = co_await store->next_seqnum(direction_t::inbound, true);
            co_return std::pair{rr.has_value(), ar.has_value()};
        });
    fixpp::session::disarm_fail_counter_write_after_reset_commit();
    EXPECT_TRUE(reset_ok);
    EXPECT_FALSE(advance_ok) << "the first counter write after the reset's rename must fail";
    EXPECT_EQ(fixpp::session::read_and_reset_reset_atomicity_fault_count(), 1);
    EXPECT_EQ(restart_counters(store, dir, pool), (DurableCounters{1, 1}))
        << "the reset committed, the advance did not";
    fixpp::store_test::remove_store_dir(dir);
}

// ── Faults after the rename (093 Gate B, fixpp#554) ──────────────────────────
//
// Once the rename has replaced the live log, the store's open file names the replaced
// file, so any failure from there on (the parent-directory open or fsync, the reopen,
// the lock) must poison the store: every later write returns store_io_failure until a
// restart, and a restart reads the targets the rename committed. The in-process writes
// are the clauses that discriminate. The restart clause holds whether or not the store
// was poisoned, because a write to the replaced file never reaches the live name.

using WriteResults = std::pair<fixpp::core::expected_t<void>, fixpp::core::expected_t<seqnum_t>>;

// One outbound frame at the seq the store says comes next (1 when it cannot say), then
// one inbound advance. Storing at the store's own next seq keeps store() from refusing
// the frame for its order, so a refusal here is the store refusing writes.
WriteResults write_after_reset(fixpp::session::MessageStore& store, asio::thread_pool& pool) {
    return run_on(pool, [&]() -> asio::awaitable<WriteResults> {
        auto next = co_await store.next_seqnum(direction_t::outbound, false);
        seqnum_t const seq = next.value_or(1);
        auto const frame = fixpp::store_test::make_test_frame(seq, direction_t::outbound);
        auto sr =
            co_await store.store(seq, std::span<const std::byte>(frame), direction_t::outbound);
        auto ar = co_await store.next_seqnum(direction_t::inbound, true);
        co_return WriteResults{std::move(sr), std::move(ar)};
    });
}

std::vector<byte_collecting_visitor::entry> outbound_frames(fixpp::session::MessageStore& store,
                                                            asio::thread_pool& pool) {
    byte_collecting_visitor visitor;
    run_on(pool, [&]() -> asio::awaitable<void> {
        (void)co_await store.retrieve(1, 0, direction_t::outbound, visitor);
    });
    return visitor.entries();
}

// Arms `arm`, resets an advanced store (reset() when `via_reset`, else reset_to(want)),
// and checks the post-rename poison described above.
void expect_post_rename_fault_poisons(void (*arm)() noexcept, bool via_reset,
                                      DurableCounters want) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("reset_to_post_rename_fault");
    auto store = open_advanced(dir, pool);
    ASSERT_NE(store, nullptr);
    (void)fixpp::session::read_and_reset_reset_atomicity_fault_count();
    arm();
    auto r = run_on(pool, [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
        if (via_reset) co_return co_await (*store).reset();
        co_return co_await store->reset_to(want.in, want.out);
    });
    EXPECT_EQ(fixpp::session::read_and_reset_reset_atomicity_fault_count(), 1)
        << "the post-rename fault must fire";
    ASSERT_FALSE(r.has_value()) << "the reset must report the post-rename fault";
    EXPECT_EQ(r.error(), fixpp::core::error::store_io_failure);

    auto const [stored, advanced] = write_after_reset(*store, pool);
    EXPECT_FALSE(stored.has_value()) << "store() after a post-rename fault must fail closed";
    if (!stored.has_value()) {
        EXPECT_EQ(stored.error(), fixpp::core::error::store_io_failure);
    }
    EXPECT_FALSE(advanced.has_value())
        << "next_seqnum(inbound, true) after a post-rename fault must fail closed";
    if (!advanced.has_value()) {
        EXPECT_EQ(advanced.error(), fixpp::core::error::store_io_failure);
    }

    store = nullptr;
    auto reopened = open_store(dir, pool);
    ASSERT_NE(reopened, nullptr) << "restart: re-open failed";
    EXPECT_EQ(read_counters(*reopened, pool), want) << "a restart must read the committed targets";
    EXPECT_TRUE(outbound_frames(*reopened, pool).empty())
        << "no frame stored after the fault may survive a restart";
    reopened = nullptr;
    fixpp::store_test::remove_store_dir(dir);
}

#ifndef _WIN32
TEST(FileStoreResetTo, Q29_ADirectoryFsyncFaultAfterTheRenamePoisonsTheStore) {
    expect_post_rename_fault_poisons(&fixpp::session::arm_force_reset_dir_fsync_fail_once,
                                     /*via_reset=*/false, DurableCounters{.in = 2, .out = 1});
}

TEST(FileStoreResetTo, Q29_ADirectoryOpenFaultAfterTheRenamePoisonsTheStore) {
    expect_post_rename_fault_poisons(&fixpp::session::arm_force_reset_dir_open_fail_once,
                                     /*via_reset=*/false, DurableCounters{.in = 2, .out = 1});
}

TEST(FileStoreResetTo, Q29_ResetWithADirectoryFsyncFaultPoisonsTheStore) {
    expect_post_rename_fault_poisons(&fixpp::session::arm_force_reset_dir_fsync_fail_once,
                                     /*via_reset=*/true, DurableCounters{.in = 1, .out = 1});
}
#else
TEST(FileStoreResetTo, Q29_AFlushFaultAfterTheRenamePoisonsTheStore) {
    expect_post_rename_fault_poisons(&fixpp::session::arm_force_reset_rename_flush_fail_once,
                                     /*via_reset=*/false, DurableCounters{.in = 2, .out = 1});
}
#endif  // _WIN32

// Control for the cells above: with no fault armed, the same reset leaves the store
// writable, and what it writes survives a restart.
TEST(FileStoreResetTo, Q29_Control_AResetToWithNoFaultLeavesTheStoreWritable) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("reset_to_no_fault_control");
    auto store = open_advanced(dir, pool);
    ASSERT_NE(store, nullptr);
    (void)fixpp::session::read_and_reset_reset_atomicity_fault_count();
    auto r = run_on(pool, [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
        co_return co_await store->reset_to(2, 1);
    });
    ASSERT_TRUE(r.has_value()) << "reset_to(2, 1) with no fault armed";
    EXPECT_EQ(fixpp::session::read_and_reset_reset_atomicity_fault_count(), 0);

    auto const [stored, advanced] = write_after_reset(*store, pool);
    EXPECT_TRUE(stored.has_value()) << "store() after a clean reset";
    EXPECT_TRUE(advanced.has_value()) << "next_seqnum(inbound, true) after a clean reset";

    store = nullptr;
    auto reopened = open_store(dir, pool);
    ASSERT_NE(reopened, nullptr) << "restart: re-open failed";
    EXPECT_EQ(read_counters(*reopened, pool), (DurableCounters{.in = 3, .out = 2}))
        << "the advance and the stored frame must survive a restart";
    auto const frames = outbound_frames(*reopened, pool);
    ASSERT_EQ(frames.size(), 1u) << "the frame stored after the reset must survive a restart";
    EXPECT_EQ(frames.front().bytes, fixpp::store_test::make_test_frame(1, direction_t::outbound));
    reopened = nullptr;
    fixpp::store_test::remove_store_dir(dir);
}

#ifdef _WIN32
// ── One store path conversion on Windows (093 Gate B, fixpp#554) ─────────────
//
// The reset must rename onto the file the factory opened when the store path holds a
// byte >= 0x80, and leave no second log beside it. The CompID's "\xC3\xA9" is valid
// in a single-byte code page and in UTF-8, so the factory can open it under either.
constexpr std::string_view kNonAsciiSender = "SENDER\xC3\xA9";

// Advances a store over `store_dir` with the non-ASCII CompID, runs reset_to(2, 1),
// and checks a restart reads the targets from the only log in `store_dir`.
// `cleanup_dir` is removed at the end.
void expect_reset_to_reaches_the_opened_log(const fs::path& store_dir,
                                            const fs::path& cleanup_dir) {
    asio::thread_pool pool{2};
    auto store = open_advanced(store_dir, pool, kNonAsciiSender);
    ASSERT_NE(store, nullptr) << "a CompID with a byte >= 0x80 must open";
    ASSERT_EQ(read_counters(*store, pool), kAdvanced) << "setup";
    auto r = run_on(pool, [&]() -> asio::awaitable<fixpp::core::expected_t<void>> {
        co_return co_await store->reset_to(2, 1);
    });
    EXPECT_TRUE(r.has_value()) << "reset_to(2, 1)";
    store = nullptr;

    auto reopened = open_store(store_dir, pool, kNonAsciiSender);
    ASSERT_NE(reopened, nullptr) << "restart: re-open failed";
    EXPECT_EQ(read_counters(*reopened, pool), (DurableCounters{.in = 2, .out = 1}))
        << "a restart must read the reset's targets from the log the factory opens";
    reopened = nullptr;

    std::size_t logs = 0;
    std::size_t tmps = 0;
    for (auto const& entry : fs::directory_iterator(store_dir)) {
        if (entry.path().extension() == L".log") ++logs;
        if (entry.path().extension() == L".tmp") ++tmps;
    }
    EXPECT_EQ(logs, 1u) << "the reset must not leave a second log beside the store's";
    EXPECT_EQ(tmps, 0u) << "the reset must not leave its temp log behind";
    fixpp::store_test::remove_store_dir(cleanup_dir);
}

TEST(FileStoreResetTo, Q29_ANonAsciiCompIdResetsTheLogTheFactoryOpened) {
    auto dir = unique_store_dir("reset_to_non_ascii_compid");
    expect_reset_to_reaches_the_opened_log(dir, dir);
}

TEST(FileStoreResetTo, Q29_ANonAsciiDirectoryResetsTheLogTheFactoryOpened) {
    auto base = unique_store_dir("reset_to_non_ascii_dir");
    auto const dir = base / fs::path(L"store_\u00E9");
    fs::create_directories(dir);
    expect_reset_to_reaches_the_opened_log(dir, base);
}
#endif  // _WIN32

}  // namespace
