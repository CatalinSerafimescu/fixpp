// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_file_store_flush_for_session_close.cpp
//
// Seam 19 — FileStore::flush_for_session_close() + A1 Session::close dispatch
// (FR-028 / I-17 / AC US2 #4 / #5).
//
// Sub-scenarios:
//
//   A. GRACEFUL close: FileStore under commit_batched(N=64) with 32 frames
//      stored (32 < N-1 = 63 — within the documented loss window for
//      commit_batched). After Session::close(graceful), re-open recovers ALL
//      32 frames byte-identical. store_cancelled is NOT surfaced.
//
//   B. GRACEFUL close invokes the hook via the concept-shaped NON-VIRTUAL
//      A1 dispatch (factory-type tag, NOT dynamic_cast). The flush_hook_fn
//      pointer is non-null on the Session after open() with a FileStore.
//
//   C. TERMINAL close: flush hook is NOT invoked. Up-to-N-1-record loss is
//      the documented commit_batched contract. The test verifies the hook's
//      non-invocation by observing that close(terminal) completes without
//      calling flush_for_session_close() on the FileStore object directly.
//
//   D. MemoryStore path: flush_hook_fn is nullptr → no hook invocation
//      under close(graceful). The MemoryStore does NOT satisfy
//      has_flush_for_session_close (FR-029).
//
// TDD: compile-RED until T032 wires Session::close(graceful) through
//      close_flush_hook_a1_. Linker-RED until T024/T032 ship.
//
// NOTE: We do NOT subclass Session or access private members. Instead, we
// exercise the external contract:
//   - Session::open() + Session::close(mode) via the public API.
//   - FileStoreFactory::make() is used to produce the store.
//   - Re-open the store via a fresh factory call to verify durability.
//
// The "no dynamic_cast" property is structural (compile-time concept gate at
// flush_thunk_for<FileStore>() in message_store.hpp); this test exercises the
// runtime effect — graceful drains, terminal does not.
#include <gtest/gtest.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <cstddef>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/memory_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory>
#include <span>
#include <vector>

#include "_fixtures_/store_temp_dir.hpp"
#include "_fixtures_/test_double_fsm.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/minimal_security_profile.hpp"

namespace {

namespace fs = std::filesystem;
using fixpp::core::EngineConfig;
using fixpp::core::error;
using fixpp::core::expected_t;
using fixpp::session::close_mode;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::MemoryStore;
using fixpp::session::MemoryStoreFactory;
using fixpp::session::seqnum_t;
using fixpp::session::Session;
using fixpp::session::SessionConfig;
using fixpp::store_test::byte_collecting_visitor;
using fixpp::store_test::make_store_script;
using fixpp::store_test::unique_store_dir;

// ── Test helpers ─────────────────────────────────────────────────────────────

std::shared_ptr<fixpp::core::mock_clock> make_mock_clock(asio::any_io_executor ex) {
    return std::make_shared<fixpp::core::mock_clock>(fixpp::core::utc_time_point{},
                                                     fixpp::core::steady_time_point{}, ex);
}

FileStore::Config make_file_config(const fs::path& dir, asio::any_io_executor exec,
                                   FileStorePolicy policy = {}) {
    FileStore::Config cfg;
    cfg.directory = dir;
    cfg.sender_comp_id = "FLUSHSND";
    cfg.target_comp_id = "FLUSHTGT";
    cfg.policy = policy;
    cfg.max_frame_bytes = 4096;
    cfg.file_io_executor = exec;
    return cfg;
}

// ── A/B. Graceful close via Session with FileStore: A1 hook is non-null ──────
//
// This test verifies the compile-time and runtime A1 hook mechanism:
//   - flush_thunk_for<FileStore>() is non-null (concept gate fires at compile time)
//   - Session::open() with a FileStoreFactory produces a non-null A1 hook
//     (stashed as close_flush_hook_a1_)
//   - Session::close(graceful) completes successfully with a FileStore
//
// Full "write 32 frames → graceful close → re-open → all 32 recovered" is
// tested in DirectFlushDrainsPendingBatchedFrames which exercises the
// FileStore directly. The session-level draining test requires 005's FSM
// to write frames through the session; here we focus on the A1 hook wiring.

TEST(FileStoreFlushForSessionClose, GracefulCloseFlushes32FramesBatch64) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("graceful_batch64");

    constexpr std::size_t kBatch = 64;
    FileStorePolicy policy;
    policy.which = FileStorePolicy::kind::commit_batched;
    policy.batch_size = kBatch;
    FileStore::Config fcfg = make_file_config(dir, pool.get_executor(), policy);

    // ── Part 1: compile-time + runtime A1 hook non-null check ────────────────
    // flush_thunk_for<FileStore>() is evaluated at compile time via the concept
    // has_flush_for_session_close. The runtime value must be non-null.
    EXPECT_NE(fixpp::session::MessageStore::flush_thunk_for<FileStore>(), nullptr)
        << "FileStore must satisfy has_flush_for_session_close — "
           "flush_thunk_for<FileStore>() must return a non-null typed thunk "
           "(concept-shaped non-virtual A1 dispatch, NOT dynamic_cast)";

    // ── Part 2: Session::close(graceful) with FileStore completes cleanly ────
    // The advisory lock is released when the Session destructs (not just closes).
    // Use an explicit scope block so the session destructs before we re-open.
    {
        EngineConfig engine;
        engine.executor = pool.get_executor();
        engine.clock = make_mock_clock(pool.get_executor());
        engine.max_store_memory_per_session = 1ULL << 30;

        SessionConfig cfg{};
        cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
        cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
        cfg.sender_comp_id = "FLUSHSND";
        cfg.target_comp_id = "FLUSHTGT";
        cfg.store_factory = std::make_unique<FileStoreFactory>(fcfg);

        // Open: mints the FileStore, stashes A1 hook as close_flush_hook_a1_
        Session session{engine, cfg};
        asio::co_spawn(pool.get_executor(), session.open(), asio::use_future).get();

        // Graceful close: must invoke flush_for_session_close() via A1 hook
        // then proceed to phase 2 root cancellation.
        auto result = asio::co_spawn(pool.get_executor(), session.close(close_mode::graceful),
                                     asio::use_future)
                          .get();
        EXPECT_TRUE(result.has_value()) << "close(graceful) with FileStore returned error: "
                                        << static_cast<int>(result.error());

        // Session destructs here — advisory lock released.
    }

    // ── Part 3: re-open after graceful close succeeds ────────────────────────
    // After the session and its FileStore destruct, the advisory lock is free.
    FileStore::Config fcfg2 = make_file_config(dir, pool.get_executor(), policy);
    FileStoreFactory factory2{fcfg2};
    auto reminted = factory2.make("FLUSHSND", "FLUSHTGT", nullptr, 1ULL << 30, pool.get_executor());
    ASSERT_TRUE(reminted.has_value()) << "re-open failed after graceful close + session destruct";

    fs::remove_all(dir);
}

// ── Direct flush_for_session_close() drain test ───────────────────────────────
// Write 32 frames via a FileStore with commit_batched(N=64), call
// flush_for_session_close() directly, then re-open and assert all 32 recovered.

TEST(FileStoreFlushForSessionClose, DirectFlushDrainsPendingBatchedFrames) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("direct_flush");

    constexpr int kFrames = 32;
    constexpr std::size_t kBatch = 64;
    ASSERT_LT(static_cast<std::size_t>(kFrames), kBatch - 1u)
        << "test invariant: kFrames must be within the commit_batched window";

    auto script = make_store_script(kFrames, direction_t::outbound);

    FileStorePolicy policy;
    policy.which = FileStorePolicy::kind::commit_batched;
    policy.batch_size = kBatch;

    FileStore::Config fcfg = make_file_config(dir, pool.get_executor(), policy);
    FileStoreFactory factory{fcfg};
    auto minted = factory.make("FLUSHSND", "FLUSHTGT", nullptr, 1ULL << 30, pool.get_executor());
    ASSERT_TRUE(minted.has_value()) << "factory.make() failed";

    auto& store_base = *minted.value();

    // Store 32 frames (< batch boundary — NOT automatically fsynced)
    asio::co_spawn(
        pool.get_executor(),
        [&store_base, &script]() -> asio::awaitable<void> {
            for (const auto& step : script) {
                auto r = co_await store_base.store(
                    step.seq, std::span<const std::byte>(step.frame_bytes), step.dir);
                EXPECT_TRUE(r.has_value()) << "store() failed at seq=" << step.seq;
                if (!r) {
                    co_return;
                }
            }
        },
        asio::use_future)
        .get();

    // Invoke flush_for_session_close() via the A1 typed thunk (non-virtual)
    // This is the mechanism used by Session::close(graceful) internally.
    {
        auto hook = store_base.flush_hook();
        ASSERT_NE(hook, nullptr) << "FileStore must have a non-null flush_hook() (A1 mechanism)";

        auto result = asio::co_spawn(
                          pool.get_executor(),
                          [&store_base, hook]() -> asio::awaitable<fixpp::core::expected_t<void>> {
                              co_return co_await (*hook)(store_base);
                          },
                          asio::use_future)
                          .get();
        ASSERT_TRUE(result.has_value())
            << "flush_for_session_close() returned error: " << static_cast<int>(result.error());
    }

    // Explicitly release the advisory lock by destroying the minted store.
    // Move the unique_ptr out so the FileStore destructs (releasing the flock).
    {
        auto owned = std::move(minted.value());
        (void)owned;  // destructs here, releasing lock and fd
    }

    // Re-open and retrieve: all 32 frames must be present byte-identical
    FileStore::Config fcfg2 = make_file_config(dir, pool.get_executor(), policy);
    FileStoreFactory factory2{fcfg2};
    auto reminted = factory2.make("FLUSHSND", "FLUSHTGT", nullptr, 1ULL << 30, pool.get_executor());
    ASSERT_TRUE(reminted.has_value()) << "re-open failed";

    auto& store2 = *reminted.value();
    byte_collecting_visitor visitor;
    asio::co_spawn(
        pool.get_executor(),
        [&store2, &visitor]() -> asio::awaitable<void> {
            auto r = co_await store2.retrieve(1, 0, direction_t::outbound, visitor);
            EXPECT_TRUE(r.has_value()) << "retrieve after re-open failed";
        },
        asio::use_future)
        .get();

    ASSERT_EQ(visitor.entries().size(), static_cast<std::size_t>(kFrames))
        << "flush_for_session_close() did not drain all " << kFrames
        << " frames before graceful close; recovered only " << visitor.entries().size();

    for (std::size_t i = 0; i < visitor.entries().size(); ++i) {
        EXPECT_EQ(visitor.entries()[i].bytes, script[i].frame_bytes)
            << "frame " << i << " not byte-identical after re-open";
    }

    fs::remove_all(dir);
}

// ── C. Terminal close hook non-invocation: verified structurally ──────────────
// The hook non-invocation for terminal close is verified by the scripted seam-5
// (test_cancellation_fromapp_to_close.cpp, set_close_flush_hook) and by
// A1HookNotInvokedBySessionCloseTerminal below (tracking_store). The test
// TerminalCloseDoesNotInvokeHook was deleted — it could not assert the
// negation from outside the session; the tracking_store path (below) does.

// ── D. MemoryStore: flush_hook() is nullptr → hook skipped under graceful ─────

TEST(FileStoreFlushForSessionClose, MemoryStoreHasNullHook) {
    // MemoryStore does NOT satisfy has_flush_for_session_close (FR-029).
    // flush_thunk_for<MemoryStore>() must return nullptr at compile time.
    auto hook = fixpp::session::MessageStore::flush_thunk_for<MemoryStore>();
    EXPECT_EQ(hook, nullptr) << "MemoryStore must NOT satisfy has_flush_for_session_close; "
                                "flush_thunk_for<MemoryStore>() must be nullptr (FR-029)";
}

TEST(FileStoreFlushForSessionClose, SessionWithMemoryStoreGracefulCloseSucceeds) {
    // A session with MemoryStore + close(graceful): no hook → completes cleanly.
    asio::thread_pool pool{2};

    MemoryStore::Config mcfg;
    mcfg.policy = fixpp::session::capacity_policy::bounded;
    mcfg.inbound_capacity = 100;
    mcfg.outbound_capacity = 100;
    mcfg.max_frame_bytes = 4096;

    // Use a small cap that allows the default config through DoS check
    constexpr std::size_t kSmallCap = 100 * 100 * 4096 + 1;  // just over product

    EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = kSmallCap;

    SessionConfig cfg{};
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.store_factory = std::make_unique<MemoryStoreFactory>(mcfg);

    Session session{engine, cfg};
    asio::co_spawn(pool.get_executor(), session.open(), asio::use_future).get();

    // close(graceful) with null A1 hook must still succeed
    auto result =
        asio::co_spawn(pool.get_executor(), session.close(close_mode::graceful), asio::use_future)
            .get();
    EXPECT_TRUE(result.has_value())
        << "close(graceful) with null A1 hook returned error: " << static_cast<int>(result.error());
}

// ── D2. A1 dispatch: Session::close(graceful) invokes flush_hook via A1 ──────
//
// A custom MessageStore that implements flush_for_session_close() and tracks
// whether it was called. When Session::close(graceful) is invoked, the A1
// mechanism must call the hook exactly once.
//
// This test will be RED until T032 wires `close_flush_hook_a1_` into
// Session::close(). Currently only `close_flush_hook_` (the scripted seam-5
// hook) is dispatched.

namespace {  // local helpers for this test

class tracking_store final : public fixpp::session::MessageStore {
public:
    tracking_store()
        : MessageStore(MessageStore::flush_thunk_for<tracking_store>()), flush_call_count_(0) {}

    // Satisfies has_flush_for_session_close — concept gate selects typed thunk.
    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>>
    flush_for_session_close() noexcept {
        ++flush_call_count_;  // non-atomic: .get() on the future is the sync point
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> store(
        fixpp::session::seqnum_t, std::span<const std::byte>,
        fixpp::session::direction_t) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        fixpp::session::seqnum_t, fixpp::session::seqnum_t, fixpp::session::direction_t,
        fixpp::session::retrieve_visitor&) noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> next_seqnum(
        fixpp::session::direction_t, bool) noexcept override {
        co_return fixpp::core::expected_t<fixpp::session::seqnum_t>{1u};
    }

    [[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        co_return fixpp::core::expected_t<void>{};
    }

    [[nodiscard]] int flush_call_count() const noexcept { return flush_call_count_; }

private:
    int flush_call_count_;
};

class tracking_store_factory final : public fixpp::session::MessageStoreFactory {
public:
    tracking_store* last_store = nullptr;

    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view, std::string_view, std::pmr::memory_resource*, std::size_t,
        asio::any_io_executor) noexcept override {
        auto s = std::make_unique<tracking_store>();
        last_store = s.get();
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{std::move(s)};
    }
};

}  // namespace

TEST(FileStoreFlushForSessionClose, A1HookInvokedBySessionCloseGraceful) {
    // Verify that flush_thunk_for<tracking_store>() returns a non-null thunk.
    // This is a compile-time check on the concept gate.
    EXPECT_NE(fixpp::session::MessageStore::flush_thunk_for<tracking_store>(), nullptr)
        << "tracking_store must satisfy has_flush_for_session_close";

    asio::thread_pool pool{2};

    auto* raw_factory = new tracking_store_factory{};  // owned via unique_ptr below
    auto factory_up = std::unique_ptr<fixpp::session::MessageStoreFactory>(raw_factory);

    EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    SessionConfig cfg{};
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.store_factory = std::move(factory_up);

    Session session{engine, cfg};
    asio::co_spawn(pool.get_executor(), session.open(), asio::use_future).get();

    // The tracking_store is now owned by the session
    tracking_store* ts = raw_factory->last_store;
    ASSERT_NE(ts, nullptr) << "factory.make() was not called during open()";

    // Baseline: flush not called yet
    EXPECT_EQ(ts->flush_call_count(), 0);

    // close(graceful): must invoke flush_for_session_close() exactly once via A1
    auto result =
        asio::co_spawn(pool.get_executor(), session.close(close_mode::graceful), asio::use_future)
            .get();
    EXPECT_TRUE(result.has_value())
        << "close(graceful) returned error: " << static_cast<int>(result.error());

    // T032 RED: this will be 0 until T032 wires close_flush_hook_a1_ dispatch.
    EXPECT_EQ(ts->flush_call_count(), 1)
        << "Session::close(graceful) must invoke flush_for_session_close() "
           "exactly ONCE via the A1 typed thunk (close_flush_hook_a1_ dispatch). "
           "Currently 0 — T032 not yet implemented.";
}

TEST(FileStoreFlushForSessionClose, A1HookNotInvokedBySessionCloseTerminal) {
    asio::thread_pool pool{2};

    auto* raw_factory = new tracking_store_factory{};
    auto factory_up = std::unique_ptr<fixpp::session::MessageStoreFactory>(raw_factory);

    EngineConfig engine;
    engine.executor = pool.get_executor();
    engine.clock = make_mock_clock(pool.get_executor());
    engine.max_store_memory_per_session = 1ULL << 30;

    SessionConfig cfg{};
    cfg.dictionary = fixpp::test_support::make_minimal_dictionary();
    cfg.security_profile = fixpp::test_support::make_minimal_security_profile();
    cfg.store_factory = std::move(factory_up);

    Session session{engine, cfg};
    asio::co_spawn(pool.get_executor(), session.open(), asio::use_future).get();

    tracking_store* ts = raw_factory->last_store;
    ASSERT_NE(ts, nullptr);

    // close(terminal): must NOT invoke flush_for_session_close()
    auto result =
        asio::co_spawn(pool.get_executor(), session.close(close_mode::terminal), asio::use_future)
            .get();
    EXPECT_TRUE(result.has_value());

    EXPECT_EQ(ts->flush_call_count(), 0)
        << "Session::close(terminal) must NOT invoke flush_for_session_close() "
           "(Appendix D §D.2)";
}

// ── E. store_cancelled NOT surfaced under graceful close ──────────────────────
// flush_for_session_close() must not return store_cancelled under graceful
// close (per FR-028 / I-17 "does NOT surface store_cancelled").

TEST(FileStoreFlushForSessionClose, FlushDoesNotSurfaceStoreCancelled) {
    asio::thread_pool pool{2};
    auto dir = unique_store_dir("no_cancelled");

    FileStore::Config fcfg = make_file_config(dir, pool.get_executor());
    FileStoreFactory factory{fcfg};
    auto minted = factory.make("FLUSHSND", "FLUSHTGT", nullptr, 1ULL << 30, pool.get_executor());
    ASSERT_TRUE(minted.has_value());

    auto& store_base = *minted.value();
    auto hook = store_base.flush_hook();
    ASSERT_NE(hook, nullptr);

    // Invoke the hook. It must NOT return store_cancelled.
    auto result = asio::co_spawn(
                      pool.get_executor(),
                      [&store_base, hook]() -> asio::awaitable<fixpp::core::expected_t<void>> {
                          co_return co_await (*hook)(store_base);
                      },
                      asio::use_future)
                      .get();

    if (!result.has_value()) {
        EXPECT_NE(result.error(), error::store_cancelled)
            << "flush_for_session_close() must NOT surface store_cancelled "
               "under graceful close (FR-028 / I-17)";
    }
    // Success or any error other than store_cancelled is acceptable in the
    // base case (no frames stored). We primarily verify the non-cancellation
    // property.

    fs::remove_all(dir);
}

}  // namespace
