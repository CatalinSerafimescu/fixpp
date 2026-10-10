// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_544_session_strand.cpp
//
// fixpp#544 (B35; `.specify/544-hot-path-zero-alloc.md` §2.1, §3 "Behaviour"): the session
// strand's fast path.
//
//   Pins (compile time): the inline-fit condition on the fast-path strand and on the
//     contingency executor's strand; on every compiler but MSVC, the fast path's inner
//     executor is io_context::executor_type; every property change of the contingency
//     executor keeps its type.
//   Target-type oracle: make_session_strand's result, and the Engine's stored session
//     strand, hold the fast-path strand type; a work-tracked executor takes the fallback.
//   Semantic cells, each run against BOTH strand forms (task A-1): the production one,
//     make_session_strand over an io_context executor, and a strand over
//     session_io_executor built directly. On a compiler where the production alias is
//     session_io_executor the two forms are the same type.
//       serialisation  two handlers on one strand never overlap;
//       equality       the session executor and a socket built on the strand compare
//                      equal to it; another strand and a re-wrapped one do not;
//       work tracking  a tracked copy keeps run() alive until released, and a pending
//                      read keeps it alive and completes on the strand.
//   Engine cells (production form): the session executor, the transport's socket executor
//     and SessionEntry::session_strand compare equal, and handlers on a live session's
//     strand never overlap.
//   session_io_executor cells: equality and queries follow the context and the property
//     bits; execute honours blocking.never; the inner executor execute() forwards to
//     carries relationship.continuation; tracked copies, moves and assignments balance the
//     outstanding work.
//
// SerialisationProbe_Control_BareExecutorOverlaps is a positive control for
// overlap_probe::body only; the comment above it states how the serialisation probe's
// ability to expose an overlap is shown.

#include <gtest/gtest.h>

#include <array>
#include <asio/any_io_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/execution.hpp>
#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/post.hpp>
#include <asio/prefer.hpp>
#include <asio/query.hpp>
#include <asio/require.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <fixpp/core/session_executor.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_config.hpp>
#include <memory>
#include <optional>
#include <system_error>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "session/plain_engine_rig.hpp"
#include "session/session_strand.hpp"
#include "support/engine_test_access.hpp"
#include "support/session_io_executor_test_access.hpp"
#include "support/wait_until.hpp"
#include "transport/asio_plain_transport.hpp"

namespace {

namespace sd = fixpp::session::detail;
using namespace std::chrono_literals;

using fast_strand_t = asio::strand<sd::session_inner_executor_t>;
using contingency_strand_t = asio::strand<sd::session_io_executor>;

// ── Pins ─────────────────────────────────────────────────────────────────────
// The inline-fit condition is spelled out here rather than taken from
// fits_any_io_executor_inline_v, so that a change to that trait cannot move both sides.
// It is any_executor_base's constructor condition against its object_type
// (`asio/execution/any_executor.hpp`).
constexpr std::size_t kInlineSize = sizeof(std::shared_ptr<void>) + sizeof(void*);
constexpr std::size_t kInlineAlign = alignof(std::shared_ptr<void>);

static_assert(sizeof(fast_strand_t) <= kInlineSize,
              "the fast-path strand does not fit any_io_executor's inline storage");
static_assert(alignof(fast_strand_t) <= kInlineAlign,
              "the fast-path strand is over-aligned for any_io_executor's inline storage");

// Task A-1: the contingency executor's strand fits on every platform, not only where
// production selects it.
static_assert(sizeof(contingency_strand_t) <= kInlineSize,
              "strand<session_io_executor> does not fit any_io_executor's inline storage");
static_assert(alignof(contingency_strand_t) <= kInlineAlign,
              "strand<session_io_executor> is over-aligned for any_io_executor's inline storage");

#ifndef _MSC_VER
// Without this pin an asio layout change could move this compiler onto the contingency
// executor silently. MSVC's layout takes the contingency executor.
static_assert(std::is_same_v<sd::session_inner_executor_t, asio::io_context::executor_type>,
              "the fast path's inner executor is not io_context::executor_type");
#endif

static_assert(asio::execution::is_executor<sd::session_io_executor>::value);
// Every property change keeps the type, so a property change cannot produce a strand type
// that might not fit.
// NOLINTBEGIN(readability-static-accessed-through-instance): asio's property-object spelling
static_assert(std::is_same_v<decltype(asio::prefer(std::declval<sd::session_io_executor>(),
                                                   asio::execution::outstanding_work.tracked)),
                             sd::session_io_executor>);
static_assert(std::is_same_v<decltype(asio::require(std::declval<sd::session_io_executor>(),
                                                    asio::execution::blocking.never)),
                             sd::session_io_executor>);
static_assert(std::is_same_v<decltype(asio::prefer(std::declval<sd::session_io_executor>(),
                                                   asio::execution::relationship.continuation)),
                             sd::session_io_executor>);
// NOLINTEND(readability-static-accessed-through-instance)

// ── Target-type oracle ───────────────────────────────────────────────────────

TEST(B35SessionStrand, TargetTypeOracle_IoContextExecutorTakesTheFastPath) {
    asio::io_context ioc;
    asio::any_io_executor const s = sd::make_session_strand(ioc.get_executor());
    EXPECT_NE(s.target<fast_strand_t>(), nullptr);
    EXPECT_EQ(s.target<asio::strand<asio::any_io_executor>>(), nullptr);
}

TEST(B35SessionStrand, TargetTypeOracle_OtherExecutorsTakeTheFallback) {
    asio::io_context ioc;
    asio::thread_pool pool{1};
    std::vector<asio::any_io_executor> const others{
        // NOLINTNEXTLINE(readability-static-accessed-through-instance): asio's spelling
        asio::prefer(ioc.get_executor(), asio::execution::outstanding_work.tracked),
        pool.get_executor(),
    };
    for (auto const& exec : others) {
        asio::any_io_executor const s = sd::make_session_strand(exec);
        EXPECT_EQ(s.target<fast_strand_t>(), nullptr);
        EXPECT_NE(s.target<asio::strand<asio::any_io_executor>>(), nullptr);
    }
    pool.join();
}

// ── The two strand forms the semantic cells run against ──────────────────────

struct production_form {
    using strand_type = fast_strand_t;
    static asio::any_io_executor make(asio::io_context& ioc) {
        return sd::make_session_strand(ioc.get_executor());
    }
};

struct contingency_form {
    using strand_type = contingency_strand_t;
    static asio::any_io_executor make(asio::io_context& ioc) {
        return asio::any_io_executor{
            asio::make_strand(sd::session_io_executor{ioc.get_executor()})};
    }
};

template <class Form>
class B35StrandSemantics : public ::testing::Test {};

struct form_names {
    template <class T>
    static std::string GetName(int) {
        return std::is_same_v<T, production_form> ? "production" : "contingency";
    }
};

using forms = ::testing::Types<production_form, contingency_form>;
TYPED_TEST_SUITE(B35StrandSemantics, forms, form_names);

// ── Serialisation ────────────────────────────────────────────────────────────

// Counts the handlers inside it at once. Each body waits, up to `partner_wait`, for a
// second body to be inside too, so an executor that lets two handlers overlap shows it.
struct overlap_probe {
    std::atomic<int> in_flight{0};
    std::atomic<int> max_seen{0};
    std::atomic<int> bodies{0};
    std::atomic<int> coroutines_done{0};

    // Stops waiting once any overlap has been seen, so a partner that has already left
    // does not keep it waiting.
    void body(std::chrono::microseconds partner_wait) {
        note(in_flight.fetch_add(1) + 1);
        auto const until = std::chrono::steady_clock::now() + partner_wait;
        while (max_seen.load() < 2 && std::chrono::steady_clock::now() < until) {
            note(in_flight.load());
            std::this_thread::yield();
        }
        in_flight.fetch_sub(1);
        bodies.fetch_add(1);
    }

    void note(int n) {
        int m = max_seen.load();
        while (n > m && !max_seen.compare_exchange_weak(m, n)) {
        }
    }
};

constexpr int kRunThreads = 4;
constexpr int kPostsPerProducer = 16;
constexpr int kCoroutines = 4;
constexpr int kSegments = 12;
// On a strand no partner ever arrives, so each body waits the whole of this.
constexpr std::chrono::microseconds kStrandPartnerWait{2000};
constexpr std::chrono::seconds kProbeBudget{60};

// A coroutine whose segments between suspensions each run one probe body. Its
// suspensions alternate a post and a zero timer, so it resumes through the strand both
// ways.
asio::awaitable<void> segmented_body(asio::any_io_executor s, overlap_probe& p) {
    asio::steady_timer t{s};
    for (int i = 0; i < kSegments; ++i) {
        p.body(kStrandPartnerWait);
        if (i % 2 == 0) {
            co_await asio::post(s, asio::use_awaitable);
        } else {
            t.expires_after(0ms);
            co_await t.async_wait(asio::use_awaitable);
        }
    }
    p.coroutines_done.fetch_add(1);
}

// Runs the probe on `s` with kRunThreads threads running `ioc`: handlers posted from two
// producer threads at once, then coroutines co_spawned on `s`. True iff every body ran.
bool run_serialisation_probe(asio::io_context& ioc, asio::any_io_executor const& s,
                             overlap_probe& p) {
    auto guard = asio::make_work_guard(ioc);
    std::vector<std::thread> runners;
    runners.reserve(kRunThreads);
    for (int i = 0; i < kRunThreads; ++i) runners.emplace_back([&ioc] { ioc.run(); });

    std::thread producer_a{[&] {
        for (int i = 0; i < kPostsPerProducer; ++i)
            asio::post(s, [&p] { p.body(kStrandPartnerWait); });
    }};
    std::thread producer_b{[&] {
        for (int i = 0; i < kPostsPerProducer; ++i)
            asio::post(s, [&p] { p.body(kStrandPartnerWait); });
    }};
    producer_a.join();
    producer_b.join();
    for (int i = 0; i < kCoroutines; ++i) asio::co_spawn(s, segmented_body(s, p), asio::detached);

    int const want_bodies = (2 * kPostsPerProducer) + (kCoroutines * kSegments);
    auto const limit = std::chrono::steady_clock::now() + kProbeBudget;
    bool done = false;
    while (std::chrono::steady_clock::now() < limit) {
        if (p.bodies.load() == want_bodies && p.coroutines_done.load() == kCoroutines) {
            done = true;
            break;
        }
        std::this_thread::sleep_for(1ms);
    }
    guard.reset();
    ioc.stop();
    for (auto& t : runners) t.join();
    return done;
}

TYPED_TEST(B35StrandSemantics, Serialisation_TwoHandlersNeverOverlap) {
    asio::io_context ioc{kRunThreads};
    asio::any_io_executor const s = TypeParam::make(ioc);
    ASSERT_NE(s.template target<typename TypeParam::strand_type>(), nullptr);
    overlap_probe p;
    bool const done = run_serialisation_probe(ioc, s, p);
    EXPECT_TRUE(done) << "not every probe body ran";
    EXPECT_EQ(p.max_seen.load(), 1) << "two handlers on one strand overlapped";
}

// The positive control for overlap_probe::body: two bodies on the bare io_context executor,
// each with a long partner wait, must see each other. It does not run run_serialisation_probe
// or kStrandPartnerWait. Their ability to expose an overlap is shown by mutation: make
// make_session_strand's fast path return the bare executor, and
// Engine_HandlersOnALiveSessionStrandNeverOverlap must fail with 'two handlers on a live
// session's strand overlapped'.
TEST(B35SessionStrand, SerialisationProbe_Control_BareExecutorOverlaps) {
    asio::io_context ioc{kRunThreads};
    auto guard = asio::make_work_guard(ioc);
    std::vector<std::thread> runners;
    // NOLINTNEXTLINE(performance-inefficient-vector-operation): a control
    for (int i = 0; i < kRunThreads; ++i) runners.emplace_back([&ioc] { ioc.run(); });
    overlap_probe p;
    // Two bodies, each waiting for the other: on an executor that runs them on two
    // threads, both see two inside.
    for (int i = 0; i < 2; ++i) asio::post(ioc.get_executor(), [&p] { p.body(10s); });
    auto const limit = std::chrono::steady_clock::now() + kProbeBudget;
    while (p.bodies.load() < 2 && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(1ms);
    }
    guard.reset();
    ioc.stop();
    for (auto& t : runners) t.join();
    EXPECT_EQ(p.bodies.load(), 2);
    EXPECT_EQ(p.max_seen.load(), 2) << "the probe could not see two handlers overlap";
}

// ── Equality ─────────────────────────────────────────────────────────────────

TYPED_TEST(B35StrandSemantics, Equality_SessionExecutorAndSocketCompareEqualToTheStrand) {
    asio::io_context ioc;
    asio::any_io_executor const s = TypeParam::make(ioc);
    asio::any_io_executor const copy = s;  // NOLINT(performance-unnecessary-copy-initialization)
    EXPECT_TRUE(copy == s);

    // The Engine path: the session executor adopts the strand without a re-wrap.
    auto const se = fixpp::core::make_session_executor(fixpp::core::adopt_strand_t{}, s, nullptr);
    EXPECT_TRUE(se.underlying() == s) << "the session executor does not equal its strand";

    // A transport's socket built on the strand.
    asio::ip::tcp::socket sock{s};
    EXPECT_TRUE(sock.get_executor() == s) << "a socket built on the strand does not equal it";

    // Another session's strand, over the same io_context.
    asio::any_io_executor const other = TypeParam::make(ioc);
    EXPECT_FALSE(other == s) << "two sessions' strands compare equal";

    // A strand re-wrapped in a second strand.
    asio::any_io_executor const rewrapped = sd::make_session_strand(s);
    EXPECT_FALSE(rewrapped == s) << "a re-wrapped strand compares equal to the original";
}

// ── Work tracking ────────────────────────────────────────────────────────────

// How long a cell watches run() for an early return. A run() that returns although work
// is outstanding returns at once, so this only has to exceed scheduling noise.
constexpr auto kStaysAliveFor = 200ms;
// A bound on how long run() may take to return once the cell's work is released.
constexpr auto kReturnBudget = 10s;

TYPED_TEST(B35StrandSemantics, WorkTracking_TrackedCopyKeepsRunAliveUntilReleased) {
    asio::io_context ioc;
    asio::any_io_executor const s = TypeParam::make(ioc);
    std::optional<asio::any_io_executor> tracked{
        // NOLINTNEXTLINE(readability-static-accessed-through-instance): asio's spelling
        asio::prefer(s, asio::execution::outstanding_work.tracked)};
    std::atomic<bool> returned{false};
    std::thread runner{[&] {
        ioc.run();
        returned.store(true, std::memory_order_release);
    }};
    // Copies, moves and assignments of the tracked executor while run() runs: each
    // must start or finish exactly the work it holds.
    {
        asio::any_io_executor a = *tracked;
        asio::any_io_executor b = std::move(a);
        asio::any_io_executor c = s;
        c = b;
        c = std::move(b);
    }
    std::this_thread::sleep_for(kStaysAliveFor);
    bool const alive_while_held = !returned.load(std::memory_order_acquire);
    tracked.reset();
    bool const returned_after_release = fixpp::test_support::wait_for_flag(returned, kReturnBudget);
    if (!returned_after_release) ioc.stop();
    runner.join();
    EXPECT_TRUE(alive_while_held) << "run() returned while a tracked executor was held";
    EXPECT_TRUE(returned_after_release) << "run() did not return once the work was released";
}

TYPED_TEST(B35StrandSemantics, WorkTracking_PendingReadKeepsRunAliveAndCompletesOnTheStrand) {
    asio::io_context ioc;
    asio::any_io_executor const s = TypeParam::make(ioc);
    auto const* const st = s.template target<typename TypeParam::strand_type>();
    ASSERT_NE(st, nullptr);

    asio::ip::tcp::acceptor acceptor{ioc, {asio::ip::make_address("127.0.0.1"), 0}};
    asio::ip::tcp::socket reader{s};
    asio::ip::tcp::socket writer{ioc};
    writer.connect(acceptor.local_endpoint());
    acceptor.accept(reader);
    acceptor.close();

    std::array<char, 1> buf{};
    std::atomic<int> ran_on_strand{-1};
    reader.async_read_some(asio::buffer(buf), [&](std::error_code const& ec, std::size_t n) {
        ran_on_strand.store(!ec && n == 1 && st->running_in_this_thread() ? 1 : 0);
    });

    std::atomic<bool> returned{false};
    std::thread runner{[&] {
        ioc.run();
        returned.store(true, std::memory_order_release);
    }};
    std::this_thread::sleep_for(kStaysAliveFor);
    bool const alive_while_pending = !returned.load(std::memory_order_acquire);
    // `writer` has no operation in flight, so a synchronous write from this thread is
    // not a concurrent operation on it.
    std::error_code wec;
    asio::write(writer, asio::buffer("x", 1), wec);
    bool const returned_after_read = fixpp::test_support::wait_for_flag(returned, kReturnBudget);
    if (!returned_after_read) ioc.stop();
    runner.join();
    EXPECT_FALSE(wec) << wec.message();
    EXPECT_TRUE(alive_while_pending) << "run() returned while a read was pending";
    EXPECT_TRUE(returned_after_read) << "run() did not return after the read completed";
    EXPECT_EQ(ran_on_strand.load(), 1) << "the read's handler did not run on the strand";
}

// ── Engine cells (production form) ───────────────────────────────────────────

namespace plain = fixpp::test_support::plain_rig;

// Brings a plain acceptor session to Active on `rig`.
bool to_active(plain::Rig& rig) { return rig.start(rig.cfg()) && rig.to_active(); }

TEST(B35SessionStrand, Engine_SessionExecutorTransportAndSessionStrandCompareEqual) {
    plain::Rig rig;
    bool const up = to_active(rig);
    auto const sess = rig.session();
    std::optional<asio::any_io_executor> entry_strand;
    std::optional<asio::any_io_executor> socket_exec;
    std::optional<asio::any_io_executor> session_exec;
    if (up && sess) {
        // No thread runs the io_context between pumps, so the registry read is safe.
        entry_strand = fixpp::session::engine_test_access::session_strand(*rig.engine, rig.id);
        session_exec = sess->executor().underlying();
        if (auto* plain_t =
                dynamic_cast<fixpp::transport::asio_plain_transport*>(&sess->live_transport())) {
            socket_exec = plain_t->socket_executor();
        }
    }
    rig.stop();
    ASSERT_TRUE(up && sess) << "the session did not reach Active";
    ASSERT_TRUE(entry_strand.has_value()) << "SessionEntry::session_strand is not set";
    ASSERT_TRUE(socket_exec.has_value()) << "the live transport is not asio_plain_transport";
    // NOLINTBEGIN(bugprone-unchecked-optional-access): each follows an ASSERT_TRUE on it
    EXPECT_NE(entry_strand->target<fast_strand_t>(), nullptr)
        << "the stored session strand is not the fast-path strand";
    EXPECT_TRUE(*session_exec == *entry_strand) << "session executor != session_strand";
    EXPECT_TRUE(*socket_exec == *entry_strand) << "transport socket executor != session_strand";
    // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(B35SessionStrand, Engine_HandlersOnALiveSessionStrandNeverOverlap) {
    plain::Rig rig;
    bool const up = to_active(rig);
    auto const sess = rig.session();
    overlap_probe p;
    bool done = false;
    if (up && sess) {
        asio::any_io_executor const s = sess->executor().underlying();
        done = run_serialisation_probe(rig.ioc, s, p);
        rig.ioc.restart();
    }
    rig.stop();
    ASSERT_TRUE(up && sess) << "the session did not reach Active";
    EXPECT_TRUE(done) << "not every probe body ran";
    EXPECT_EQ(p.max_seen.load(), 1) << "two handlers on a live session's strand overlapped";
}

// ── session_io_executor ──────────────────────────────────────────────────────

TEST(B35SessionIoExecutor, EqualityAndQueriesFollowContextAndBits) {
    asio::io_context ioc;
    asio::io_context other_ioc;
    sd::session_io_executor const e{ioc.get_executor()};
    EXPECT_EQ(&asio::query(e, asio::execution::context), &ioc);
    EXPECT_TRUE(e == sd::session_io_executor{ioc.get_executor()});
    EXPECT_FALSE(e == sd::session_io_executor{other_ioc.get_executor()});
    EXPECT_TRUE(asio::query(e, asio::execution::blocking) == asio::execution::blocking.possibly);
    EXPECT_TRUE(asio::query(e, asio::execution::relationship) ==
                asio::execution::relationship.fork);
    EXPECT_TRUE(asio::query(e, asio::execution::outstanding_work) ==
                asio::execution::outstanding_work.untracked);

    // NOLINTBEGIN(readability-static-accessed-through-instance): asio's property-object spelling
    auto const never = asio::require(e, asio::execution::blocking.never);
    EXPECT_TRUE(asio::query(never, asio::execution::blocking) == asio::execution::blocking.never);
    EXPECT_FALSE(never == e);
    EXPECT_TRUE(asio::require(never, asio::execution::blocking.possibly) == e);

    auto const cont = asio::prefer(e, asio::execution::relationship.continuation);
    EXPECT_TRUE(asio::query(cont, asio::execution::relationship) ==
                asio::execution::relationship.continuation);
    EXPECT_TRUE(asio::prefer(cont, asio::execution::relationship.fork) == e);

    auto const tracked = asio::prefer(e, asio::execution::outstanding_work.tracked);
    EXPECT_TRUE(asio::query(tracked, asio::execution::outstanding_work) ==
                asio::execution::outstanding_work.tracked);
    EXPECT_TRUE(asio::prefer(tracked, asio::execution::outstanding_work.untracked) == e);

    // Built from an io_context executor, it copies the source's blocking and relationship.
    sd::session_io_executor const from_never{
        asio::require(ioc.get_executor(), asio::execution::blocking.never)};
    EXPECT_TRUE(from_never == never);
    sd::session_io_executor const from_cont{
        asio::require(ioc.get_executor(), asio::execution::relationship.continuation)};
    // NOLINTEND(readability-static-accessed-through-instance)
    EXPECT_TRUE(from_cont == cont);
}

TEST(B35SessionIoExecutor, ExecuteHonoursBlockingNever) {
    asio::io_context ioc;
    sd::session_io_executor const e{ioc.get_executor()};
    // NOLINTNEXTLINE(readability-static-accessed-through-instance): asio's spelling
    auto const never = asio::require(e, asio::execution::blocking.never);
    // Every flag lives in this frame: the blocking.never function runs after the
    // handler that submitted it has returned.
    bool possibly_ran = false;
    bool possibly_inline = false;
    bool never_ran = false;
    bool never_inline = true;
    asio::post(ioc, [&] {
        e.execute([&] { possibly_ran = true; });
        possibly_inline = possibly_ran;
        never.execute([&] { never_ran = true; });
        never_inline = never_ran;
    });
    ioc.run();
    EXPECT_TRUE(possibly_inline) << "blocking.possibly did not run inline inside the io_context";
    EXPECT_FALSE(never_inline) << "blocking.never ran inline";
    EXPECT_TRUE(never_ran) << "the blocking.never function never ran";
}

// execute() forwards to inner(), an io_context executor rebuilt from the property bits; the
// relationship bit must reach it, because the io_context executor's post reads it. No public
// operation of session_io_executor returns the inner executor, and the bit has no effect
// every scheduler shows (the IOCP io_context ignores it), so the cell queries inner()
// through the test-access friend. Mutant: delete the relationship.continuation `require`
// in inner(); the continuation query must then fail.
TEST(B35SessionIoExecutor, InnerExecutorCarriesContinuation) {
    asio::io_context ioc;
    sd::session_io_executor const e{ioc.get_executor()};
    auto const cont = asio::require(e, asio::execution::relationship_t::continuation);

    auto const fork_inner = sd::session_io_executor_test_access::inner(e);
    EXPECT_TRUE(asio::query(fork_inner, asio::execution::relationship) ==
                asio::execution::relationship_t::fork);

    auto const cont_inner = sd::session_io_executor_test_access::inner(cont);
    EXPECT_TRUE(asio::query(cont_inner, asio::execution::relationship) ==
                asio::execution::relationship_t::continuation)
        << "inner() dropped relationship.continuation";
    EXPECT_TRUE(asio::query(cont_inner, asio::execution::blocking) ==
                asio::execution::blocking_t::possibly);
    EXPECT_EQ(&asio::query(cont_inner, asio::execution::context), &ioc);

    // A handler executed through the continuation executor, and one through its inner
    // executor, both run on the io_context.
    bool via_cont = false;
    bool via_inner = false;
    cont.execute([&] { via_cont = true; });
    cont_inner.execute([&] { via_inner = true; });
    ioc.run();
    EXPECT_TRUE(via_cont) << "a handler executed through the continuation executor never ran";
    EXPECT_TRUE(via_inner) << "a handler executed through its inner executor never ran";
}

// One operation on tracked and untracked executors over one io_context. `tracked` is a
// tracked executor the caller keeps; `untracked` is not tracked.
using work_op = void (*)(sd::session_io_executor const& tracked,
                         sd::session_io_executor const& untracked);

// Runs `op` while run() runs, then checks that run() stays alive while the caller's own
// tracked executor is held (no over-finish) and returns once it is released (no leak).
// One operation per run, so a leak in one cannot hide an over-finish in another.
void expect_balanced(work_op op) {
    asio::io_context ioc;
    sd::session_io_executor const e{ioc.get_executor()};
    std::optional<sd::session_io_executor> keep{
        // NOLINTNEXTLINE(readability-static-accessed-through-instance): asio's spelling
        asio::prefer(e, asio::execution::outstanding_work.tracked)};
    std::atomic<bool> returned{false};
    std::thread runner{[&] {
        ioc.run();
        returned.store(true, std::memory_order_release);
    }};
    op(*keep, e);
    std::this_thread::sleep_for(kStaysAliveFor);
    bool const alive_while_held = !returned.load(std::memory_order_acquire);
    keep.reset();
    bool const returned_after_release = fixpp::test_support::wait_for_flag(returned, kReturnBudget);
    if (!returned_after_release) ioc.stop();
    runner.join();
    EXPECT_TRUE(alive_while_held) << "the operation finished work it did not hold";
    EXPECT_TRUE(returned_after_release) << "the operation leaked outstanding work";
}

// NOLINTBEGIN(bugprone-use-after-move,clang-analyzer-cplusplus.Move) — moved-from use is the
// subject
TEST(B35SessionIoExecutor, TrackedCopiesMovesAndAssignmentsBalanceWork) {
    struct named_op {
        char const* name;
        work_op op;
    };
    // NOLINTBEGIN(modernize-use-designated-initializers): name, then op
    // NOLINTBEGIN(performance-unnecessary-copy-initialization): the copy is the op
    // NOLINTBEGIN(readability-static-accessed-through-instance): asio's spelling
    named_op const ops[] = {
        {"copy-construct", [](auto const& t, auto const&) { sd::session_io_executor a = t; }},
        {"move-construct",
         [](auto const& t, auto const&) {
             sd::session_io_executor a = t;
             sd::session_io_executor b = std::move(a);
         }},
        {"copy-assign tracked onto untracked",
         [](auto const& t, auto const& u) {
             sd::session_io_executor a = u;
             a = t;
         }},
        {"copy-assign untracked onto tracked",
         [](auto const& t, auto const& u) {
             sd::session_io_executor a = t;
             a = u;
         }},
        {"copy-assign tracked onto tracked",
         [](auto const& t, auto const&) {
             sd::session_io_executor a = t;
             a = t;
         }},
        {"move-assign tracked onto untracked",
         [](auto const& t, auto const& u) {
             sd::session_io_executor a = u;
             sd::session_io_executor b = t;
             a = std::move(b);
         }},
        {"move-assign untracked onto tracked",
         [](auto const& t, auto const& u) {
             sd::session_io_executor a = t;
             sd::session_io_executor b = u;
             a = std::move(b);
         }},
        {"move-assign tracked onto a moved-from",
         [](auto const& t, auto const&) {
             sd::session_io_executor a = t;
             sd::session_io_executor b = std::move(a);
             sd::session_io_executor c = t;
             a = std::move(c);
         }},
        {"self-assign",
         [](auto const& t, auto const&) {
             sd::session_io_executor a = t;
             auto& alias = a;
             a = alias;
             a = std::move(alias);
         }},
        {"require tracked, then untracked",
         [](auto const&, auto const& u) {
             auto const a = asio::require(u, asio::execution::outstanding_work.tracked);
             auto const b = asio::require(a, asio::execution::outstanding_work.untracked);
         }},
    };
    // NOLINTEND(readability-static-accessed-through-instance)
    // NOLINTEND(performance-unnecessary-copy-initialization)
    // NOLINTEND(modernize-use-designated-initializers)
    for (auto const& o : ops) {
        SCOPED_TRACE(o.name);
        expect_balanced(o.op);
    }
}
// NOLINTEND(bugprone-use-after-move,clang-analyzer-cplusplus.Move)

}  // namespace
