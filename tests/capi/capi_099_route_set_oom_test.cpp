// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/capi/capi_099_route_set_oom_test.cpp
//
// 099-fixt-appl-version-validation, contracts/witnesses.md W-20 (tasks.md T034), FR-028: a route
// set's build failure is `out_of_memory`, from fixt_route_set::build, Engine::start() and a
// directly constructed session's open(), and never a route with a hole. Its mutant is M-44.
//
// A fail-at-N global operator new, armed around the call, on FIXT11 plus a minimal application
// dictionary, because a real one's copy makes a sweep take hours (research R-3). Not
// pugi::set_memory_management_functions: as_table_view() reads a finished handle, so pugixml never
// runs and that hook would never fire. Each arm fails unless the hook fired and its sweep saw both
// outcomes, since an unfired hook reads green. tasks.md T108 adds the C arm.

#include <gtest/gtest.h>

#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/io_context.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fixpp/core/engine_config.hpp>
#include <fixpp/core/error.hpp>
#include <fixpp/core/test/mock_clock.hpp>
#include <fixpp/dict/dictionary.hpp>
#include <fixpp/dict/load_any.hpp>
#include <fixpp/dict/version_profile.hpp>
#include <fixpp/dict/version_registry.hpp>
#include <fixpp/session/engine.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <future>
#include <memory>
#include <memory_resource>
#include <new>
#include <ranges>
#include <span>
#include <vector>

#include "dictionary_internal.hpp"  // as_table_view_call_count, read by the hook below
#include "session/fixt_route_set.hpp"
#include "support/engine_test_access.hpp"
#include "support/minimal_dictionary.hpp"
#include "support/session_test_access.hpp"

// ASan, TSan and MSan own operator new. Under them this file is one skipped cell.
#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#endif
#if !defined(FIXPP_SANITIZER_REPLACES_NEW) && \
    (defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__))
#define FIXPP_SANITIZER_REPLACES_NEW 1
#endif
#ifndef FIXPP_SANITIZER_REPLACES_NEW
#define FIXPP_SANITIZER_REPLACES_NEW 0
#endif

#if !FIXPP_SANITIZER_REPLACES_NEW
namespace {

// The whole of the test runs on one thread, the one that pumps its io_context; atomics only so
// that a stray worker of a library thread reads no torn value.
std::atomic<bool> g_armed{false};
std::atomic<long> g_seen{0};        // allocations since arming
std::atomic<long> g_fail_at{0};     // 0: none fails
std::atomic<long> g_fired{0};       // how many the hook failed
std::atomic<long> g_first_walk{0};  // the allocation at which the walk counter first moved
std::atomic<std::uint64_t> g_walks_at_arm{0};
std::atomic<long> g_watch_at{0};           // 0: none is watched
std::atomic<std::size_t> g_watch_size{0};  // the size of the allocation at g_watch_at

void arm(long fail_at) {
    g_seen.store(0);
    g_fired.store(0);
    g_first_walk.store(0);
    g_fail_at.store(fail_at);
    g_walks_at_arm.store(fixpp::dict::detail::as_table_view_call_count());
    g_armed.store(true);
}

void disarm() { g_armed.store(false); }

}  // namespace

// GCC's -Wmismatched-new-delete pairs std::free with the STANDARD operator new; these replace it.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void* operator new(std::size_t size) {
    if (g_armed.load()) {
        const long seen = g_seen.fetch_add(1) + 1;
        if (g_first_walk.load() == 0 &&
            fixpp::dict::detail::as_table_view_call_count() != g_walks_at_arm.load()) {
            g_first_walk.store(seen);
        }
        if (seen == g_watch_at.load()) {
            g_watch_size.store(size);
        }
        if (seen == g_fail_at.load()) {
            g_fired.fetch_add(1);
            throw std::bad_alloc{};
        }
    }
    void* p = std::malloc(size != 0U ? size : 1U);
    if (p == nullptr) {
        throw std::bad_alloc{};
    }
    return p;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace fixpp::session {
namespace {

namespace dd = fixpp::dict;
using fixpp::core::error;
using fixpp::core::expected_t;
using DictPtr = std::shared_ptr<const dd::Dictionary>;
using Set = detail::fixt_route_set;

DictPtr fixt11() {
    static std::pmr::monotonic_buffer_resource mr;
    static const DictPtr d = std::make_shared<const dd::Dictionary>(
        dd::load_any(std::filesystem::path{FIXPP_DICT_DATA_DIR} / "FIXT11.xml", &mr));
    return d;
}

DictPtr minimal_application() {
    static const DictPtr d = fixpp::test_support::make_minimal_dictionary();  // FIX 4.2
    return d;
}

std::vector<DictPtr> engine_dictionaries() { return {fixt11(), minimal_application()}; }

SessionConfig fixt_config() {
    SessionConfig c;
    c.sender_comp_id = "ISLD";
    c.target_comp_id = "TW";
    c.begin_string = "FIXT.1.1";
    c.heartbeat_interval = std::chrono::seconds{30};
    c.security_profile = SecurityProfile{SecurityProfile::kind::one_way_ca};
    c.dictionary = fixt11();
    c.role = session_role::acceptor;
    c.reset_seqnum_policy_field = reset_seqnum_policy::bilateral_lenient;
    c.default_appl_ver_id = dd::application_version::v42;
    c.validate_inbound_messages = true;
    c.transport_send = [](std::span<const std::byte>) {};
    return c;
}

// The allocations of one fault-free build, and the one at which its first walk starts.
struct BuildCount {
    long allocations = 0;
    long first_walk = 0;
};

BuildCount count_build() {
    const dd::version_registry registry{engine_dictionaries()};
    arm(0);
    auto built = Set::build(registry, std::views::single(fixt11().get()));
    disarm();
    const BuildCount c{.allocations = g_seen.load(), .first_walk = g_first_walk.load()};
    EXPECT_TRUE(built.has_value());
    return c;
}

TEST(W20Build, EveryFiredAllocationIsOutOfMemoryAndTheLastIsAUsableSet) {
    const dd::version_registry registry{engine_dictionaries()};
    long fired_runs = 0;
    long completed_runs = 0;
    for (long n = 1;; ++n) {
        arm(n);
        auto built = Set::build(registry, std::views::single(fixt11().get()));
        disarm();
        if (g_fired.load() == 0) {
            ++completed_runs;
            ASSERT_TRUE(built.has_value()) << "N " << n << ": no allocation failed";
            const Set::entry* const fixt = (*built)->find(fixt11().get(), nullptr);
            const Set::entry* const app = (*built)->find(minimal_application().get(), nullptr);
            ASSERT_NE(fixt, nullptr);
            ASSERT_NE(app, nullptr);
            EXPECT_EQ(fixt->owner.get(), &fixt->validator->table())
                << "the owner must alias the validator's own table";
            break;
        }
        ++fired_runs;
        ASSERT_FALSE(built.has_value()) << "N " << n << ": a failed build published a set";
        EXPECT_EQ(built.error(), error::out_of_memory) << "N " << n;
    }
    EXPECT_GT(fired_runs, 0) << "the hook never fired, so this arm proved nothing";
    EXPECT_EQ(completed_runs, 1);
}

// An Engine with one FIXT session over the Build arm's registry and S, so that the build's own
// count is this start()'s. start() allocates nothing ahead of the build.
class EngineRig {
public:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    std::unique_ptr<Engine> engine;

    EngineRig() {
        const auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        fixpp::core::EngineConfig cfg;
        cfg.executor = ioc.get_executor();
        cfg.clock = clock;
        cfg.dictionaries = engine_dictionaries();
        engine = std::make_unique<Engine>(ioc.get_executor(), std::move(cfg));
    }
    EngineRig(const EngineRig&) = delete;
    EngineRig& operator=(const EngineRig&) = delete;
    EngineRig(EngineRig&&) = delete;
    EngineRig& operator=(EngineRig&&) = delete;
    ~EngineRig() {
        auto fut = asio::co_spawn(ioc, engine->stop(), asio::use_future);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (fut.wait_for(std::chrono::seconds{0}) != std::future_status::ready &&
               std::chrono::steady_clock::now() < end) {
            ioc.restart();
            ioc.poll();
        }
        if (fut.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            std::abort();  // ~Engine would abort on its precondition anyway
        }
    }
};

TEST(W20Engine, EachFiredAllocationFailsStartThenASecondStartSucceeds) {
    const BuildCount build = count_build();
    ASSERT_GT(build.allocations, 0);
    long fired_runs = 0;
    long retries_started = 0;
    for (long n = 1; n <= build.allocations; ++n) {
        EngineRig rig;
        EXPECT_TRUE(rig.engine->register_session(fixt_config()).has_value());
        arm(n);
        const auto first = rig.engine->start();
        disarm();
        fired_runs += g_fired.load() != 0 ? 1 : 0;
        EXPECT_EQ(g_fired.load(), 1) << "N " << n << " is past this start()'s build";
        EXPECT_FALSE(first.has_value()) << "N " << n;
        if (first.has_value()) {
            continue;  // that start() spawned its loops, so a second one is not this arm's
        }
        EXPECT_EQ(first.error(), error::out_of_memory) << "N " << n;
        EXPECT_EQ(engine_test_access::outstanding_loops(*rig.engine), 0) << "N " << n;
        EXPECT_EQ(engine_test_access::routes(*rig.engine), nullptr) << "N " << n;
        const auto second = rig.engine->start();
        retries_started += second.has_value() ? 1 : 0;
        EXPECT_TRUE(second.has_value()) << "N " << n;
        EXPECT_NE(engine_test_access::routes(*rig.engine), nullptr) << "N " << n;
        EXPECT_EQ(engine_test_access::outstanding_loops(*rig.engine), 1) << "N " << n;
    }
    EXPECT_EQ(fired_runs, build.allocations) << "an unfired hook reads green";
    EXPECT_EQ(retries_started, build.allocations) << "the sweep must see both outcomes";
}

// A session opened with the hook armed only around the co_await: open() is a noexcept coroutine and
// its frame allocation is outside FR-028, so the awaitable is constructed disarmed.
asio::awaitable<expected_t<void>> armed_open(asio::awaitable<expected_t<void>> open, long fail_at) {
    arm(fail_at);
    auto r = co_await std::move(open);
    disarm();
    co_return r;
}

class DirectRig {
public:
    asio::io_context ioc;
    std::shared_ptr<fixpp::core::mock_clock> clock;
    fixpp::core::EngineConfig engine;
    dd::version_registry registry{engine_dictionaries()};
    std::unique_ptr<Session> session;

    DirectRig() {
        const auto utc = std::chrono::system_clock::time_point{} + std::chrono::seconds{1704067200};
        clock = std::make_shared<fixpp::core::mock_clock>(utc, fixpp::core::steady_time_point{},
                                                          ioc.get_executor());
        engine.clock = clock;
        engine.executor = ioc.get_executor();
        SessionConfig c = fixt_config();
        c.executor_override = ioc.get_executor();
        session = std::make_unique<Session>(engine, c, &registry);
    }

    template <class Awaitable>
    [[nodiscard]] expected_t<void> run(Awaitable a) {
        auto fut = asio::co_spawn(ioc, std::move(a), asio::use_future);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        while (fut.wait_for(std::chrono::seconds{0}) != std::future_status::ready &&
               std::chrono::steady_clock::now() < end) {
            ioc.restart();
            ioc.poll();
        }
        if (fut.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
            return std::unexpected(error::dispatch_aborted);
        }
        return fut.get();
    }
};

// A dry run: the allocations of open() ahead of its build. Past the build and the reservation,
// open()'s other allocations throw through its noexcept, so N stays inside those two.
long allocations_ahead_of_the_build(const BuildCount& build) {
    DirectRig rig;
    const auto opened = rig.run(armed_open(rig.session->open(), 0));
    EXPECT_TRUE(opened.has_value());
    return g_first_walk.load() - build.first_walk;
}

TEST(W20Direct, EachFiredAllocationOfTheBuildFailsOpenThenARetryOpens) {
    const BuildCount build = count_build();
    ASSERT_GT(build.allocations, 0);
    const long ahead = allocations_ahead_of_the_build(build);
    ASSERT_GE(ahead, 0);

    long fired_runs = 0;
    long retries_opened = 0;
    for (long n = ahead + 1; n <= ahead + build.allocations; ++n) {
        DirectRig rig;
        const auto first = rig.run(armed_open(rig.session->open(), n));
        fired_runs += g_fired.load() != 0 ? 1 : 0;
        EXPECT_EQ(g_fired.load(), 1) << "N " << n;
        EXPECT_FALSE(first.has_value()) << "N " << n;
        if (!first.has_value()) {
            EXPECT_EQ(first.error(), error::out_of_memory) << "N " << n;
        }
        EXPECT_EQ(session_test_access::lifecycle_value(*rig.session), 0)
            << "N " << n << ": a refused open() leaves never_opened";
        const auto retry = rig.run(rig.session->open());
        retries_opened += retry.has_value() ? 1 : 0;
        EXPECT_TRUE(retry.has_value()) << "N " << n;
    }
    EXPECT_EQ(fired_runs, build.allocations) << "an unfired hook reads green";
    EXPECT_EQ(retries_opened, build.allocations) << "the sweep must see both outcomes";
}

// data-model E-10's block: a failed reservation is FR-028's out_of_memory too. It is the allocation
// after the build, by E-9's order, and kReservationBytes spells its size from E-10's block. A dry
// run reads that allocation first, so that an N landing on any other allocation of open() is a
// failed assertion here and not a throw through its noexcept.
TEST(W20Direct, TheReservationFailsOpenThenARetryOpens) {
    constexpr std::size_t kReservationBytes = 256U * 10U;
    const BuildCount build = count_build();
    ASSERT_GT(build.allocations, 0);
    const long ahead = allocations_ahead_of_the_build(build);
    ASSERT_GE(ahead, 0);
    const long n = ahead + build.allocations + 1;
    {
        g_watch_at.store(n);
        g_watch_size.store(0);
        DirectRig rig;
        const auto opened = rig.run(armed_open(rig.session->open(), 0));
        g_watch_at.store(0);
        ASSERT_TRUE(opened.has_value());
        ASSERT_EQ(g_watch_size.load(), kReservationBytes)
            << "allocation " << n << " of open() is not its reservation";
    }
    DirectRig rig;
    const auto first = rig.run(armed_open(rig.session->open(), n));
    EXPECT_EQ(g_fired.load(), 1) << "N " << n;
    ASSERT_FALSE(first.has_value());
    EXPECT_EQ(first.error(), error::out_of_memory);
    EXPECT_EQ(session_test_access::lifecycle_value(*rig.session), 0)
        << "a refused open() leaves never_opened";
    const auto retry = rig.run(rig.session->open());
    EXPECT_TRUE(retry.has_value());
}

}  // namespace
}  // namespace fixpp::session

#else  // FIXPP_SANITIZER_REPLACES_NEW

TEST(W20, ThisSanitizerOwnsOperatorNew) { GTEST_SKIP() << "the fail-at-N hook replaces it"; }

#endif
