// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/session/test_598_store_offload.cpp
//
// fixpp#598 (.specify/536-537-538-recovery-liveness.md §11, D-598-1 to D-598-6; §5, the
// table "The store's offload (§11)"; §11.5): once a FileStore call holds the store's lock,
// no cancellation reaches the job its offload hands to the file executor. The cells on a
// bare FileStore, and those on a directly driven Session on the callback transport. The
// cells under an Engine are in test_598_store_offload_engine.cpp.
//
// The fixture (tests/support/recording_file_executor.hpp): a FileStore whose file executor
// wraps a pool of two threads, hands every job to the pool at once, and holds the pool thread
// that takes the call's first job before that thread starts it, until the cell lets it go.
// "Taken and not started" is three events, none of them a time:
//   1. a pool thread has taken the call's write job, and the executor holds that thread;
//   2. a cancellation with the terminal bit is emitted on the call's slot, from a handler on
//      the io_context that runs the call (the test thread), which is not a pool thread;
//   3. the release event: the emit has returned, the count of jobs handed over for the call
//      has been read, and every job of the call other than the held one has ended. Only then
//      is the held thread let go.
// The emit is made inside a handler of the call's io_context so that asio's cancellation
// handlers run inside it: a dispatch onto an io_context from a thread that is not running it
// would queue, and the count read after the emit would not be final.
//
// What each RED-first cell on that executor asserts beside its row: the body began exactly
// once, after the cell let the job go (the store's offload probe); and no job beyond the
// write's own was handed over for the call. The second is a zero, and a zero is never the
// evidence that the cancellation was delivered. The evidence is the witness: after the call
// has returned or thrown, its own coroutine reads a cancellation with the terminal bit on its
// thread. A cell asserts from the executor's record that its window was hit before it asserts
// anything else; a cell whose window was not hit fails as "the path was not exercised".
//
// A control records the count and does not assert it. The pin of the lock's grant (fixpp#583)
// and the control at the lock have their own evidence that the emit arrived.
//
// The suites:
//   OffloadTakenNotStarted      store, next_seqnum with increment, reset (§5's first three
//                               rows of §11).
//   OffloadReopen               a reopened store, after a later write per shape, and with no
//                               later write (R-27's premise).
//   OffloadOneThreadControl     the same three operations on a pool of one thread held by a
//                               job of the cell: the order of the jobs is kept.
//   OffloadTwoJobsLetGoTogether both threads held by jobs of the cell while the write's job
//                               and the cancellation's job are queued, then let go by one
//                               event. Two arms; the seeded one carries the known positive of
//                               the measurement under ThreadSanitizer (§11.5).
//   LockGrantPin                granted, and the waiter not yet resumed (§11.3, T2a).
//   OffloadDirectSession        the graceful Logout and an application's send on a directly
//                               driven Session on the callback transport, and their control
//                               on one thread.
//
// FS-RECORD lines on stdout carry each cell's readings, printed before any assertion of the
// cell, so that a RED run still has its record. Re-derive them from a run's output with
//   grep '^FS-RECORD' <output>

#include <gtest/gtest.h>

#include <algorithm>
#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/cancellation_state.hpp>
#include <asio/cancellation_type.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/system_error.hpp>
#include <asio/this_coro.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fixpp/core/error.hpp>
#include <fixpp/session/application.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "session/logon_arm_fixtures.hpp"
#include "session/plain_engine_rig.hpp"
#include "support/calling_thread_readings.hpp"
#include "support/pump_until_ready.hpp"
#include "support/recording_file_executor.hpp"
#include "support/session_test_access.hpp"
#include "support/temp_dir.hpp"

namespace {

namespace fe = fixpp::test_support::file_executor;
namespace plain_rig = fixpp::test_support::plain_rig;
using fixpp::core::error;
using fixpp::session::close_mode;
using fixpp::session::direction_t;
using fixpp::session::FileStore;
using fixpp::session::FileStoreFactory;
using fixpp::session::FileStorePolicy;
using fixpp::session::fsm_state;
using fixpp::session::seqnum_t;
using fixpp::session::session_test_access;
using fixpp::test_support::CallingThreadReadings;
using fixpp::test_support::kPumpBudget;
using fixpp::test_support::pump_until;
using fixpp::test_support::logon_arm::DirectAcceptor;
using steady = std::chrono::steady_clock;

using bytes = std::vector<std::byte>;

constexpr char const* kSender = "ISLD";
constexpr char const* kTarget = "TW";

// The log's record format, spelled from src/session/file_store.cpp and not read from it: a
// 16-byte header, then the payload, padded to 8 bytes. A frame of make_frame()'s 8 bytes is
// one 24-byte record, and a counter record (an 8-byte payload) is another.
constexpr std::uintmax_t kFrameRecord = 24;
constexpr std::uintmax_t kCounterRecord = 24;

// An 8-byte frame whose bytes name its number, so that a hole's zeros cannot pass for it.
bytes make_frame(seqnum_t seq) {
    bytes b(8, std::byte{0xA5});
    std::memcpy(b.data(), &seq, sizeof(seq));
    return b;
}

std::string hex(bytes const& b) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (auto const x : b) {
        auto const v = static_cast<unsigned>(x);
        out += kDigits[(v >> 4U) & 0xFU];
        out += kDigits[v & 0xFU];
    }
    return out.empty() ? std::string{"(empty)"} : out;
}

std::string error_text(std::optional<error> const& e) {
    return e ? "error " + std::to_string(static_cast<int>(*e)) : std::string{"success"};
}

// ── What the call under test did, and what its coroutine read afterwards ─────────────

struct CallReading {
    bool done = false;
    bool returned = false;
    std::optional<error> err;
    std::optional<seqnum_t> value;
    bool threw = false;
    bool threw_operation_aborted = false;
    asio::cancellation_type after = asio::cancellation_type::none;

    // The witness: a cancellation with the terminal bit is recorded on the call's thread.
    [[nodiscard]] bool witness() const {
        return (after & asio::cancellation_type::terminal) != asio::cancellation_type::none;
    }
    [[nodiscard]] bool succeeded() const { return returned && !err; }
    [[nodiscard]] std::string text() const {
        std::string t;
        if (!done) {
            t = "did not complete";
        } else if (threw) {
            t = threw_operation_aborted ? "threw operation_aborted" : "threw";
        } else {
            t = "returned " + error_text(err);
            if (value) t += " value " + std::to_string(*value);
        }
        return t + ", thread's cancellation " + std::to_string(static_cast<unsigned>(after));
    }
};

template <class T>
asio::awaitable<void> await_and_read(
    std::function<asio::awaitable<fixpp::core::expected_t<T>>()> make_call,
    std::shared_ptr<CallReading> out) {
    try {
        auto r = co_await make_call();
        out->returned = true;
        if (!r) {
            out->err = r.error();
        } else {
            if constexpr (std::is_same_v<T, seqnum_t>) out->value = *r;
        }
    } catch (asio::system_error const& e) {
        out->threw = true;
        out->threw_operation_aborted = e.code() == asio::error::operation_aborted;
    } catch (...) {
        out->threw = true;
    }
    // The next statement after the call, or the first after the handler.
    // Read into a local first: inside a template, GCC 13 cannot type a member call on the
    // result of a co_await written in the same expression.
    auto const state = co_await asio::this_coro::cancellation_state;
    out->after = state.cancelled();
    out->done = true;
}

// Copies every frame retrieve hands it.
class CopyingVisitor final : public fixpp::session::retrieve_visitor {
public:
    std::vector<std::pair<seqnum_t, bytes>> frames;
    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t seq, std::span<const std::byte> frame) noexcept override {
        frames.emplace_back(seq, bytes(frame.begin(), frame.end()));
        co_return fixpp::session::visit_result::cont;
    }
};

struct Retrieved {
    bool ran = false;
    std::optional<error> err;
    std::vector<std::pair<seqnum_t, bytes>> frames;

    [[nodiscard]] bool exactly(seqnum_t seq, bytes const& b) const {
        return ran && !err && frames.size() == 1 && frames.front().first == seq &&
               frames.front().second == b;
    }
    [[nodiscard]] bool none() const { return ran && !err && frames.empty(); }
    [[nodiscard]] std::string text() const {
        if (!ran) return "did not run";
        if (err) return error_text(err);
        std::string t = std::to_string(frames.size()) + " frame(s)";
        for (auto const& [s, b] : frames) t += " [" + std::to_string(s) + ": " + hex(b) + "]";
        return t;
    }
};

// ── The bare-store fixture ──────────────────────────────────────────────────────────

enum class FileExec : std::uint8_t {
    two_threads_write_delayed,   // the delaying executor on a pool of two threads
    one_thread_held_by_the_cell  // a pool of one thread, held by a job of the cell
};

class BareStore {
public:
    // Declared first: the log outlives the probe, the pool and every job.
    std::shared_ptr<fe::JobLog> log = std::make_shared<fe::JobLog>();

private:
    struct temp_dir {
        std::filesystem::path path = fixpp::test_support::unique_temp_dir("b28_598_store");
        temp_dir() = default;
        ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir_;
    fe::scoped_body_probe probe_{*log};
    asio::thread_pool pool_;
    // Outlive the io_context: a call that did not complete is destroyed with it, and its
    // slot is cleared then.
    std::vector<std::shared_ptr<asio::cancellation_signal>> signals_;

public:
    std::unique_ptr<FileStore> store;
    // After the store: destroyed first, so a coroutine destroyed with it finds the store.
    asio::io_context ioc;
    FileExec const kind;

    explicit BareStore(FileExec k)
        : pool_{k == FileExec::two_threads_write_delayed ? 2U : 1U}, kind{k} {}
    BareStore(BareStore const&) = delete;
    BareStore& operator=(BareStore const&) = delete;
    // Every held thread is let go and the pool joined before any member is destroyed.
    ~BareStore() {
        try {
            log->let_go();
            pool_.join();
        } catch (...) {  // NOLINT(bugprone-empty-catch): nothing to report to from here
        }
    }

    [[nodiscard]] std::filesystem::path log_path() const {
        return dir_.path / (std::string{kSender} + "__" + kTarget + ".log");
    }
    [[nodiscard]] std::uintmax_t log_size() const {
        std::error_code ec;
        auto const n = std::filesystem::file_size(log_path(), ec);
        return ec ? 0 : n;
    }

    // Opens the store through FileStoreFactory, as a session does. On a second call the
    // store is destroyed first and opened again on the same path.
    [[nodiscard]] fixpp::core::expected_t<void> open() {
        store.reset();
        FileStore::Config c;
        c.directory = dir_.path;
        c.sender_comp_id = kSender;
        c.target_comp_id = kTarget;
        c.max_frame_bytes = 4096;
        c.policy = FileStorePolicy{.which = FileStorePolicy::kind::commit_per_message};
        c.file_io_executor = fe::DelayingExecutor{pool_.get_executor(), log,
                                                  kind == FileExec::two_threads_write_delayed};
        FileStoreFactory factory{c};
        auto ms = factory.make(kSender, kTarget, nullptr, std::size_t{1} << 20U, nullptr);
        if (!ms) return std::unexpected(ms.error());
        store.reset(static_cast<FileStore*>(ms->release()));
        return {};
    }

    void hold_a_thread() { log->hold_a_thread(pool_); }

    [[nodiscard]] std::shared_ptr<asio::cancellation_signal> new_signal() {
        signals_.push_back(std::make_shared<asio::cancellation_signal>());
        return signals_.back();
    }

    // Runs a call that is not under test to its end.
    template <class T>
    std::optional<fixpp::core::expected_t<T>> run(
        std::function<asio::awaitable<fixpp::core::expected_t<T>>()> make_call) {
        auto out = std::make_shared<std::optional<fixpp::core::expected_t<T>>>();
        asio::co_spawn(
            ioc, [out, make_call]() -> asio::awaitable<void> { *out = co_await make_call(); },
            asio::detached);
        (void)pump_until(ioc, [out] { return out->has_value(); });
        return *out;
    }

    [[nodiscard]] Retrieved retrieve(seqnum_t begin, seqnum_t end) {
        auto visitor = std::make_shared<CopyingVisitor>();
        Retrieved r;
        auto const res = run<void>([this, visitor, begin, end] {
            return store->retrieve(begin, end, direction_t::outbound, *visitor);
        });
        if (!res) return r;
        r.ran = true;
        if (!res->has_value()) r.err = res->error();
        r.frames = visitor->frames;
        return r;
    }
    [[nodiscard]] std::optional<seqnum_t> next(direction_t dir) {
        auto const r =
            run<seqnum_t>([this, dir] { return store->next_seqnum(dir, /*increment=*/false); });
        if (!r || !r->has_value()) return std::nullopt;
        return **r;
    }
    [[nodiscard]] bool store_frame(seqnum_t seq, bytes const& frame) {
        auto const r = run<void>([this, seq, &frame] {
            return store->store(seq, std::span<const std::byte>{frame}, direction_t::outbound);
        });
        return r && r->has_value();
    }
};

std::string opt_text(std::optional<seqnum_t> const& v) {
    return v ? std::to_string(*v) : std::string{"?"};
}

// ── The window "taken and not started" on a bare store ──────────────────────────────

struct WindowReading {
    bool reached = false;  // two threads: job 1 taken and held; one thread: job 1 handed over
    bool held_at_emit = false;
    bool job1_began_at_emit = false;
    bool emitted = false;
    int count = 0;  // jobs handed over for the call, read when the emit had returned
    bool others_ended = false;
    bool returned_before_let_go = false;
    CallReading call;
    int bodies = 0;
    int bodies_after_let_go = 0;
    bool job2_ended_after_job1 = false;  // read from the executor's record
    bool timed_out = false;
    std::string events;

    [[nodiscard]] bool exercised(FileExec kind) const {
        bool const common = reached && emitted && !job1_began_at_emit && !timed_out && call.done;
        if (kind == FileExec::two_threads_write_delayed) {
            return common && held_at_emit && others_ended;
        }
        return common;
    }
    [[nodiscard]] std::string text() const {
        return "reached=" + std::to_string(reached) +
               " held_at_emit=" + std::to_string(held_at_emit) +
               " job1_began_at_emit=" + std::to_string(job1_began_at_emit) +
               " emitted=" + std::to_string(emitted) + " count=" + std::to_string(count) +
               " others_ended=" + std::to_string(others_ended) +
               " returned_before_let_go=" + std::to_string(returned_before_let_go) +
               " bodies=" + std::to_string(bodies) +
               " bodies_after_let_go=" + std::to_string(bodies_after_let_go) +
               " job2_ended_after_job1=" + std::to_string(job2_ended_after_job1) +
               " timed_out=" + std::to_string(timed_out) + " call: " + call.text() +
               "\nthe executor's record:\n" + events;
    }
};

template <class T>
WindowReading run_window(BareStore& b,
                         std::function<asio::awaitable<fixpp::core::expected_t<T>>()> make_call) {
    WindowReading w;
    bool const two = b.kind == FileExec::two_threads_write_delayed;
    b.log->arm();
    if (!two) {
        b.hold_a_thread();
        if (!b.log->wait_holders(1)) return w;
    }
    auto const sig = b.new_signal();
    auto const call = std::make_shared<CallReading>();
    asio::co_spawn(b.ioc, await_and_read<T>(std::move(make_call), call),
                   asio::bind_cancellation_slot(sig->slot(), asio::detached));

    // Event 1.
    w.reached = pump_until(b.ioc, [&] { return two ? b.log->held() : b.log->handed() >= 1; });
    if (w.reached) {
        // Event 2, on the io_context's thread, inside one of its handlers.
        asio::post(b.ioc, [&] {
            w.held_at_emit = b.log->held();
            w.job1_began_at_emit = b.log->job_began(1);
            sig->emit(asio::cancellation_type::terminal);
            w.count = b.log->handed();
            w.emitted = true;
        });
        (void)pump_until(b.ioc, [&] { return w.emitted; });
        // Event 3: on two threads, every job but the held one has ended. On one thread no job
        // of the call has started.
        w.others_ended = two ? b.log->wait_others_ended() : true;
        b.ioc.poll();
        b.ioc.restart();
        w.returned_before_let_go = call->done;
    }
    b.log->let_go();
    (void)pump_until(b.ioc, [call] { return call->done; });
    w.call = *call;
    w.bodies = b.log->bodies();
    w.bodies_after_let_go = b.log->bodies_after_let_go();
    w.timed_out = b.log->held_timed_out();
    w.events = b.log->events();
    auto const e1 = w.events.find("job 1 ends");
    auto const e2 = w.events.find("job 2 ends");
    w.job2_ended_after_job1 = e1 != std::string::npos && e2 != std::string::npos && e1 < e2;
    return w;
}

// What every cell on the window asserts of the body: it began exactly once, after the cell
// let the job go.
void expect_body_once_after_let_go(WindowReading const& w) {
    EXPECT_EQ(w.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(w.bodies_after_let_go, 1) << "the store's body did not begin after the release";
}

// What a RED-first cell on the delaying executor asserts of the count.
void expect_no_job_beyond_the_writes_own(WindowReading const& w) {
    EXPECT_EQ(w.count, 1) << "a job beyond the write's own was handed to the file executor "
                             "for the call (count read when the emit had returned)";
}

void expect_window_common(WindowReading const& w) {
    EXPECT_TRUE(w.call.witness())
        << "the call's thread reads no cancellation with the terminal bit after the call: "
        << w.call.text();
    EXPECT_FALSE(w.returned_before_let_go) << "the call returned before its write job was let go";
}

// ── store, next_seqnum with increment, reset: one runner per operation ───────────────

struct StoreOutcome {
    bool opened = false;
    WindowReading w;
    std::uintmax_t size_before = 0;
    std::uintmax_t size_after = 0;
    Retrieved read;
    std::optional<seqnum_t> next_out;
};

StoreOutcome run_store_op(FileExec kind, char const* cell) {
    StoreOutcome o;
    BareStore b{kind};
    o.opened = b.open().has_value();
    if (!o.opened) return o;
    auto const frame = make_frame(1);
    o.size_before = b.log_size();
    o.w = run_window<void>(b, [&b, &frame] {
        return b.store->store(1, std::span<const std::byte>{frame}, direction_t::outbound);
    });
    o.size_after = b.log_size();
    o.read = b.retrieve(1, 1);
    o.next_out = b.next(direction_t::outbound);
    std::cout << "FS-RECORD " << cell << ": count=" << o.w.count
              << " witness=" << o.w.call.witness() << " call=" << o.w.call.text()
              << " bodies=" << o.w.bodies << "/" << o.w.bodies_after_let_go
              << " size=" << o.size_before << "->" << o.size_after
              << " retrieve(1)=" << o.read.text() << " next_out=" << opt_text(o.next_out)
              << " job2_ended_after_job1=" << o.w.job2_ended_after_job1 << "\n";
    return o;
}

void expect_store_op(StoreOutcome const& o) {
    EXPECT_TRUE(o.w.call.succeeded()) << "store() " << o.w.call.text();
    EXPECT_EQ(o.size_after - o.size_before, kFrameRecord + kCounterRecord)
        << "the log did not grow by a frame record and a counter record: " << o.size_before
        << " -> " << o.size_after;
    EXPECT_TRUE(o.read.exactly(1, make_frame(1)))
        << "retrieve of the stored number: " << o.read.text();
    EXPECT_EQ(o.next_out, std::optional<seqnum_t>{2}) << "next_seqnum(outbound, false)";
}

struct NextSeqnumOutcome {
    bool opened = false;
    WindowReading w;
    std::uintmax_t size_before = 0;
    std::uintmax_t size_after = 0;
    std::optional<seqnum_t> next_in_live;  // before the store is opened again
    bool reopened = false;
    std::optional<seqnum_t> next_in_reopened;
};

NextSeqnumOutcome run_next_seqnum_op(FileExec kind, char const* cell) {
    NextSeqnumOutcome o;
    BareStore b{kind};
    o.opened = b.open().has_value();
    if (!o.opened) return o;
    o.size_before = b.log_size();
    o.w = run_window<seqnum_t>(
        b, [&b] { return b.store->next_seqnum(direction_t::inbound, /*increment=*/true); });
    o.size_after = b.log_size();
    o.next_in_live = b.next(direction_t::inbound);
    o.reopened = b.open().has_value();
    if (o.reopened) o.next_in_reopened = b.next(direction_t::inbound);
    std::cout << "FS-RECORD " << cell << ": count=" << o.w.count
              << " witness=" << o.w.call.witness() << " call=" << o.w.call.text()
              << " bodies=" << o.w.bodies << "/" << o.w.bodies_after_let_go
              << " size=" << o.size_before << "->" << o.size_after
              << " next_in_live=" << opt_text(o.next_in_live) << " reopened=" << o.reopened
              << " next_in_reopened=" << opt_text(o.next_in_reopened)
              << " job2_ended_after_job1=" << o.w.job2_ended_after_job1 << "\n";
    return o;
}

void expect_next_seqnum_op(NextSeqnumOutcome const& o) {
    EXPECT_TRUE(o.w.call.succeeded() && o.w.call.value == std::optional<seqnum_t>{1})
        << "next_seqnum(inbound, true) must return the old value: " << o.w.call.text();
    EXPECT_EQ(o.size_after - o.size_before, kCounterRecord)
        << "the log did not grow by a counter record: " << o.size_before << " -> " << o.size_after;
    EXPECT_TRUE(o.reopened) << "the store did not open again on its path";
    EXPECT_EQ(o.next_in_reopened, std::optional<seqnum_t>{2})
        << "a store opened again on the path does not report the advanced counter";
}

struct ResetOutcome {
    bool opened = false;
    bool seeded = false;
    WindowReading w;
    std::uintmax_t fresh_size = 0;  // the log as the factory first created it
    std::uintmax_t size_after = 0;
    Retrieved read;  // every outbound frame from 1
    std::optional<seqnum_t> next_out;
    std::optional<seqnum_t> next_in;
    // Whether a later store of the next outbound number succeeds: read, not asserted.
    bool later_store_ok = false;
};

ResetOutcome run_reset_op(FileExec kind, char const* cell) {
    ResetOutcome o;
    BareStore b{kind};
    o.opened = b.open().has_value();
    if (!o.opened) return o;
    o.fresh_size = b.log_size();
    o.seeded = b.store_frame(1, make_frame(1)) && b.store_frame(2, make_frame(2));
    if (!o.seeded) return o;
    o.w = run_window<void>(b, [&b] { return b.store->reset(); });
    o.size_after = b.log_size();
    o.read = b.retrieve(1, 0);
    o.next_out = b.next(direction_t::outbound);
    o.next_in = b.next(direction_t::inbound);
    if (o.next_out) o.later_store_ok = b.store_frame(*o.next_out, make_frame(*o.next_out));
    std::cout << "FS-RECORD " << cell << ": count=" << o.w.count
              << " witness=" << o.w.call.witness() << " call=" << o.w.call.text()
              << " bodies=" << o.w.bodies << "/" << o.w.bodies_after_let_go
              << " fresh_size=" << o.fresh_size << " size_after=" << o.size_after
              << " retrieve(1..)=" << o.read.text() << " next_out=" << opt_text(o.next_out)
              << " next_in=" << opt_text(o.next_in) << " later_store_ok=" << o.later_store_ok
              << " job2_ended_after_job1=" << o.w.job2_ended_after_job1 << "\n";
    return o;
}

void expect_reset_op(ResetOutcome const& o) {
    EXPECT_TRUE(o.w.call.succeeded()) << "reset() " << o.w.call.text();
    EXPECT_TRUE(o.read.none()) << "a frame is retrievable after the reset: " << o.read.text();
    EXPECT_EQ(o.next_out, std::optional<seqnum_t>{1}) << "next outbound number after the reset";
    EXPECT_EQ(o.next_in, std::optional<seqnum_t>{1}) << "next inbound number after the reset";
    EXPECT_EQ(o.size_after, o.fresh_size)
        << "the live log is not a fresh one: its size is not the size of the log the factory "
           "first created";
}

// ── D-598-1: the three operations, taken and not started (TFS.4, TFS.5, TFS.6) ──────

TEST(OffloadTakenNotStarted, Store_IsWrittenAndReadsBack) {
    auto const o = run_store_op(FileExec::two_threads_write_delayed, "TFS.4 store");
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.w.exercised(FileExec::two_threads_write_delayed))
        << "the path was not exercised: " << o.w.text();
    expect_window_common(o.w);
    expect_store_op(o);
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

TEST(OffloadTakenNotStarted, NextSeqnumWithIncrement_IsWrittenAndSurvivesAReopen) {
    auto const o = run_next_seqnum_op(FileExec::two_threads_write_delayed, "TFS.5 next_seqnum");
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.w.exercised(FileExec::two_threads_write_delayed))
        << "the path was not exercised: " << o.w.text();
    expect_window_common(o.w);
    expect_next_seqnum_op(o);
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

TEST(OffloadTakenNotStarted, Reset_ReplacesTheLog) {
    auto const o = run_reset_op(FileExec::two_threads_write_delayed, "TFS.6 reset");
    ASSERT_TRUE(o.opened && o.seeded) << "setup: the store did not open or take two frames";
    ASSERT_TRUE(o.w.exercised(FileExec::two_threads_write_delayed))
        << "the path was not exercised: " << o.w.text();
    expect_window_common(o.w);
    expect_reset_op(o);
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

// ── D-598-1, D-598-6: a reopened store (TFS.7, TFS.8) ────────────────────────────────
//
// The first call is the one cancelled while its job is taken and not started. The second
// is an ordinary call. Then the store is destroyed and a FileStore is opened on the same
// path through the factory. Every reading is taken, and printed, before any assertion.

enum class LaterWrite : std::uint8_t {
    store_then_store,
    store_then_next_seqnum,
    next_seqnum_then_store,
    store_then_reset,
};

char const* later_write_name(LaterWrite in) {
    switch (in) {
        case LaterWrite::store_then_store:
            return "store, then store";
        case LaterWrite::store_then_next_seqnum:
            return "store, then next_seqnum with increment";
        case LaterWrite::next_seqnum_then_store:
            return "next_seqnum with increment, then store";
        case LaterWrite::store_then_reset:
            return "store, then reset";
    }
    return "?";
}

struct ReopenOutcome {
    bool opened = false;
    WindowReading w;
    bool later_ok = false;
    // Before the store is destroyed.
    Retrieved first;  // the first call's number (outbound 1), when it is a store
    Retrieved later;  // the later frame's number, when the later call is a store
    Retrieved all;    // every outbound frame from 1
    std::optional<seqnum_t> next_out;
    std::optional<seqnum_t> next_in;
    // After the store is opened again.
    std::optional<error> open_error;
    bool reopened = false;
    Retrieved all_after;
    std::optional<seqnum_t> next_out_after;
    std::optional<seqnum_t> next_in_after;
};

ReopenOutcome run_reopen(std::optional<LaterWrite> const later, char const* cell) {
    ReopenOutcome o;
    BareStore b{FileExec::two_threads_write_delayed};
    o.opened = b.open().has_value();
    if (!o.opened) return o;
    bool const first_is_store = !later || *later != LaterWrite::next_seqnum_then_store;
    auto const frame1 = make_frame(1);
    if (first_is_store) {
        o.w = run_window<void>(b, [&b, &frame1] {
            return b.store->store(1, std::span<const std::byte>{frame1}, direction_t::outbound);
        });
    } else {
        o.w = run_window<seqnum_t>(
            b, [&b] { return b.store->next_seqnum(direction_t::inbound, /*increment=*/true); });
    }
    seqnum_t later_seq = 0;
    if (later) {
        switch (*later) {
            case LaterWrite::store_then_store:
                later_seq = 2;
                o.later_ok = b.store_frame(2, make_frame(2));
                break;
            case LaterWrite::store_then_next_seqnum: {
                auto const r = b.run<seqnum_t>([&b] {
                    return b.store->next_seqnum(direction_t::inbound, /*increment=*/true);
                });
                o.later_ok = r && r->has_value();
                break;
            }
            case LaterWrite::next_seqnum_then_store:
                later_seq = 1;
                o.later_ok = b.store_frame(1, frame1);
                break;
            case LaterWrite::store_then_reset: {
                auto const r = b.run<void>([&b] { return b.store->reset(); });
                o.later_ok = r && r->has_value();
                break;
            }
        }
    }
    if (first_is_store) o.first = b.retrieve(1, 1);
    if (later_seq != 0) o.later = b.retrieve(later_seq, later_seq);
    o.all = b.retrieve(1, 0);
    o.next_out = b.next(direction_t::outbound);
    o.next_in = b.next(direction_t::inbound);
    auto const r = b.open();
    o.reopened = r.has_value();
    if (!r) o.open_error = r.error();
    if (o.reopened) {
        o.all_after = b.retrieve(1, 0);
        o.next_out_after = b.next(direction_t::outbound);
        o.next_in_after = b.next(direction_t::inbound);
    }
    std::cout << "FS-RECORD " << cell << " ["
              << (later ? later_write_name(*later) : "no later write") << "]: count=" << o.w.count
              << " witness=" << o.w.call.witness() << " first call=" << o.w.call.text()
              << " bodies=" << o.w.bodies << "/" << o.w.bodies_after_let_go
              << " later call ok=" << o.later_ok
              << " | before destroy: retrieve(first)=" << o.first.text()
              << " retrieve(later)=" << o.later.text() << " retrieve(1..)=" << o.all.text()
              << " next_out=" << opt_text(o.next_out) << " next_in=" << opt_text(o.next_in)
              << " | open again: "
              << (o.reopened ? std::string{"success"} : error_text(o.open_error))
              << " retrieve(1..)=" << o.all_after.text()
              << " next_out=" << opt_text(o.next_out_after)
              << " next_in=" << opt_text(o.next_in_after) << "\n";
    return o;
}

void expect_reopen_gate(ReopenOutcome const& o) {
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.w.exercised(FileExec::two_threads_write_delayed))
        << "the path was not exercised: " << o.w.text();
}

TEST(OffloadReopen, AfterALaterWrite_StoreThenStore) {
    auto const o = run_reopen(LaterWrite::store_then_store, "TFS.7");
    expect_reopen_gate(o);
    expect_window_common(o.w);
    EXPECT_TRUE(o.w.call.succeeded()) << "the first store() " << o.w.call.text();
    EXPECT_TRUE(o.later_ok) << "the later store() failed";
    EXPECT_TRUE(o.first.exactly(1, make_frame(1)))
        << "before the store is destroyed, retrieve of the first call's number: " << o.first.text();
    EXPECT_TRUE(o.reopened) << "the store did not open again: " << error_text(o.open_error);
    EXPECT_TRUE(o.all_after.ran && !o.all_after.err && o.all_after.frames.size() == 2 &&
                o.all_after.frames[0].second == make_frame(1) &&
                o.all_after.frames[1].second == make_frame(2))
        << "after the open, every frame the two calls stored: " << o.all_after.text();
    EXPECT_EQ(o.next_out_after, std::optional<seqnum_t>{3}) << "next outbound after the open";
    EXPECT_EQ(o.next_in_after, std::optional<seqnum_t>{1}) << "next inbound after the open";
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

TEST(OffloadReopen, AfterALaterWrite_StoreThenNextSeqnum) {
    auto const o = run_reopen(LaterWrite::store_then_next_seqnum, "TFS.7");
    expect_reopen_gate(o);
    expect_window_common(o.w);
    EXPECT_TRUE(o.w.call.succeeded()) << "the first store() " << o.w.call.text();
    EXPECT_TRUE(o.later_ok) << "the later next_seqnum() failed";
    EXPECT_TRUE(o.first.exactly(1, make_frame(1)))
        << "before the store is destroyed, retrieve of the first call's number: " << o.first.text();
    EXPECT_TRUE(o.reopened) << "the store did not open again: " << error_text(o.open_error);
    EXPECT_TRUE(o.all_after.ran && !o.all_after.err && o.all_after.frames.size() == 1 &&
                o.all_after.frames[0].second == make_frame(1))
        << "after the open, the frame the first call stored: " << o.all_after.text();
    EXPECT_EQ(o.next_out_after, std::optional<seqnum_t>{2}) << "next outbound after the open";
    EXPECT_EQ(o.next_in_after, std::optional<seqnum_t>{2}) << "next inbound after the open";
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

TEST(OffloadReopen, AfterALaterWrite_NextSeqnumThenStore) {
    auto const o = run_reopen(LaterWrite::next_seqnum_then_store, "TFS.7");
    expect_reopen_gate(o);
    expect_window_common(o.w);
    EXPECT_TRUE(o.w.call.succeeded() && o.w.call.value == std::optional<seqnum_t>{1})
        << "the first next_seqnum() " << o.w.call.text();
    EXPECT_TRUE(o.later_ok) << "the later store() failed";
    EXPECT_TRUE(o.later.exactly(1, make_frame(1)))
        << "before the store is destroyed, retrieve of the later frame: " << o.later.text();
    EXPECT_TRUE(o.reopened) << "the store did not open again: " << error_text(o.open_error);
    EXPECT_TRUE(o.all_after.ran && !o.all_after.err && o.all_after.frames.size() == 1 &&
                o.all_after.frames[0].second == make_frame(1))
        << "after the open, the later frame: " << o.all_after.text();
    EXPECT_EQ(o.next_in_after, std::optional<seqnum_t>{2}) << "next inbound after the open";
    EXPECT_EQ(o.next_out_after, std::optional<seqnum_t>{2}) << "next outbound after the open";
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

TEST(OffloadReopen, AfterALaterWrite_StoreThenReset) {
    auto const o = run_reopen(LaterWrite::store_then_reset, "TFS.7");
    expect_reopen_gate(o);
    expect_window_common(o.w);
    EXPECT_TRUE(o.w.call.succeeded()) << "the first store() " << o.w.call.text();
    EXPECT_TRUE(o.later_ok) << "the later reset() failed";
    EXPECT_TRUE(o.all.none()) << "after the reset, a frame is retrievable: " << o.all.text();
    EXPECT_EQ(o.next_out, std::optional<seqnum_t>{1}) << "next outbound after the reset";
    EXPECT_EQ(o.next_in, std::optional<seqnum_t>{1}) << "next inbound after the reset";
    EXPECT_TRUE(o.reopened) << "the store did not open again: " << error_text(o.open_error);
    EXPECT_TRUE(o.all_after.none())
        << "after the open, a frame is retrievable: " << o.all_after.text();
    EXPECT_EQ(o.next_out_after, std::optional<seqnum_t>{1}) << "next outbound after the open";
    EXPECT_EQ(o.next_in_after, std::optional<seqnum_t>{1}) << "next inbound after the open";
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

TEST(OffloadReopen, WithNoLaterWrite) {
    auto const o = run_reopen(std::nullopt, "TFS.8");
    expect_reopen_gate(o);
    expect_window_common(o.w);
    EXPECT_TRUE(o.w.call.succeeded()) << "store() " << o.w.call.text();
    EXPECT_TRUE(o.reopened) << "the store did not open again: " << error_text(o.open_error);
    EXPECT_TRUE(o.all_after.ran && !o.all_after.err && o.all_after.frames.size() == 1 &&
                o.all_after.frames[0].first == 1 && o.all_after.frames[0].second == make_frame(1))
        << "after the open, the frame: " << o.all_after.text();
    EXPECT_EQ(o.next_out_after, std::optional<seqnum_t>{2}) << "next outbound after the open";
    expect_body_once_after_let_go(o.w);
    expect_no_job_beyond_the_writes_own(o.w);
}

// ── D-598-1, control: one thread, the order kept (TFS.9) ─────────────────────────────
//
// The pool's one thread is held by a job of the cell; the call's write job, and the
// cancellation's job if there is one, queue behind it in the order they were handed over.
// The count is recorded and not asserted. No mutant: on one thread the cancellation's job
// runs after the write's, with the slot or without it.

TEST(OffloadOneThreadControl, Store) {
    auto const o = run_store_op(FileExec::one_thread_held_by_the_cell, "TFS.9 store");
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.w.exercised(FileExec::one_thread_held_by_the_cell))
        << "the path was not exercised: " << o.w.text();
    expect_window_common(o.w);
    expect_store_op(o);
    expect_body_once_after_let_go(o.w);
}

TEST(OffloadOneThreadControl, NextSeqnumWithIncrement) {
    auto const o = run_next_seqnum_op(FileExec::one_thread_held_by_the_cell, "TFS.9 next_seqnum");
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.w.exercised(FileExec::one_thread_held_by_the_cell))
        << "the path was not exercised: " << o.w.text();
    expect_window_common(o.w);
    expect_next_seqnum_op(o);
    expect_body_once_after_let_go(o.w);
}

TEST(OffloadOneThreadControl, Reset) {
    auto const o = run_reset_op(FileExec::one_thread_held_by_the_cell, "TFS.9 reset");
    ASSERT_TRUE(o.opened && o.seeded) << "setup: the store did not open or take two frames";
    ASSERT_TRUE(o.w.exercised(FileExec::one_thread_held_by_the_cell))
        << "the path was not exercised: " << o.w.text();
    expect_window_common(o.w);
    expect_reset_op(o);
    expect_body_once_after_let_go(o.w);
}

// ── D-598-1: the two jobs on two threads, let go together (TFS.15) ───────────────────
//
// Both threads of a pool of two are held by jobs of the cell while the write's job and the
// cancellation's job are queued; one event lets both go, so the two jobs can run on two
// threads at once. The release event of the cells above is not used here: waiting for the
// cancellation's job to end before the write's job starts would order the two. The executor
// is the second form of recording_file_executor.hpp, which takes no lock.
//
// The seeded arm turns on the seeded pair: a plain int of the cell's, written by the
// executor just before it invokes the second job handed over for a call, and read just
// before it invokes the first. The write is made only when a second job exists. Under
// ThreadSanitizer the pair is reported when the fixture leaves the two jobs unordered up to
// those points; that is the condition under which a silent run says anything about asio's
// own pair (§11.5, "The known positive").

constexpr int kTogetherRepetitions = 40;

struct TogetherRep {
    bool reached = false;
    bool emitted = false;
    int count = 0;
    int bodies = 0;
    CallReading call;
};

struct TogetherOutcome {
    bool opened = false;
    bool holders_ok = true;
    std::vector<TogetherRep> reps;
};

TogetherOutcome run_together(bool seeded, char const* cell) {
    TogetherOutcome o;
    fe::UnorderedLog log;
    int seed = 0;
    if (seeded) log.seed = &seed;
    fe::scoped_unordered_body_probe probe{log};
    struct temp_dir {
        std::filesystem::path path = fixpp::test_support::unique_temp_dir("b28_598_together");
        temp_dir() = default;
        ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir;
    std::vector<std::shared_ptr<asio::cancellation_signal>> signals;
    asio::thread_pool pool{2};
    // After the pool: lets the holders go on every exit, before the pool is joined.
    struct go_on_exit {
        fe::UnorderedLog& l;
        ~go_on_exit() { l.go.store(true, std::memory_order_relaxed); }
    } const go_guard{log};
    std::unique_ptr<FileStore> store;
    asio::io_context ioc;

    FileStore::Config c;
    c.directory = dir.path;
    c.sender_comp_id = kSender;
    c.target_comp_id = kTarget;
    c.max_frame_bytes = 4096;
    c.policy = FileStorePolicy{.which = FileStorePolicy::kind::commit_per_message};
    c.file_io_executor = fe::UnorderedExecutor{pool.get_executor(), &log};
    {
        FileStoreFactory factory{c};
        auto ms = factory.make(kSender, kTarget, nullptr, std::size_t{1} << 20U, nullptr);
        if (!ms) return o;
        store.reset(static_cast<FileStore*>(ms->release()));
    }
    o.opened = true;

    auto const spin_until = [](auto pred) {
        auto const until = steady::now() + kPumpBudget;
        while (!pred()) {
            if (steady::now() >= until) return false;
            std::this_thread::yield();
        }
        return true;
    };

    for (int i = 0; i < kTogetherRepetitions; ++i) {
        TogetherRep rep;
        auto const seq = static_cast<seqnum_t>(i + 1);
        auto const frame = std::make_shared<bytes>(make_frame(seq));
        log.hold_both(pool);
        if (!spin_until([&] { return log.holders_in.load(std::memory_order_relaxed) == 2; })) {
            o.holders_ok = false;
            break;
        }
        log.arm();
        signals.push_back(std::make_shared<asio::cancellation_signal>());
        auto const sig = signals.back();
        auto const call = std::make_shared<CallReading>();
        asio::co_spawn(ioc,
                       await_and_read<void>(
                           [&store, frame, seq] {
                               return store->store(seq, std::span<const std::byte>{*frame},
                                                   direction_t::outbound);
                           },
                           call),
                       asio::bind_cancellation_slot(sig->slot(), asio::detached));
        rep.reached =
            pump_until(ioc, [&] { return log.handed.load(std::memory_order_relaxed) >= 1; });
        if (rep.reached) {
            asio::post(ioc, [&] {
                sig->emit(asio::cancellation_type::terminal);
                rep.count = log.handed.load(std::memory_order_relaxed);
                rep.emitted = true;
            });
            (void)pump_until(ioc, [&] { return rep.emitted; });
        }
        // The one event that lets both threads go.
        log.go.store(true, std::memory_order_relaxed);
        (void)pump_until(ioc, [call] { return call->done; });
        if (!spin_until([&] { return log.holders_out.load(std::memory_order_relaxed) == 2; })) {
            o.holders_ok = false;
        }
        rep.bodies = log.bodies.load(std::memory_order_relaxed);
        log.disarm();
        rep.call = *call;
        o.reps.push_back(rep);
        if (!o.holders_ok) break;
    }
    if (log.holder_timed_out.load(std::memory_order_relaxed)) o.holders_ok = false;

    int skipped = 0;
    int counted_two = 0;
    int witnessed = 0;
    for (auto const& r : o.reps) {
        skipped += static_cast<int>(r.bodies == 0);
        counted_two += static_cast<int>(r.count >= 2);
        witnessed += static_cast<int>(r.call.witness());
    }
    std::cout << "FS-RECORD " << cell << ": repetitions=" << o.reps.size()
              << " body_skipped=" << skipped << " count_two_or_more=" << counted_two
              << " witnessed=" << witnessed << " holders_ok=" << o.holders_ok << "\n";
    return o;
}

void expect_together(TogetherOutcome const& o) {
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.holders_ok) << "the path was not exercised: the cell's jobs did not hold, or "
                                 "let go, both threads of the pool";
    ASSERT_EQ(o.reps.size(), static_cast<std::size_t>(kTogetherRepetitions));
    for (std::size_t i = 0; i < o.reps.size(); ++i) {
        auto const& r = o.reps[i];
        ASSERT_TRUE(r.reached && r.emitted && r.call.done)
            << "repetition " << i << ": the path was not exercised: reached=" << r.reached
            << " emitted=" << r.emitted << " call: " << r.call.text();
        EXPECT_TRUE(r.call.witness())
            << "repetition " << i
            << ": the call's thread reads no cancellation with the terminal bit: " << r.call.text();
        EXPECT_LE(r.bodies, 1) << "repetition " << i << ": the body ran more than once";
        EXPECT_EQ(r.count, 1) << "repetition " << i
                              << ": a job beyond the write's own was handed over";
    }
}

TEST(OffloadTwoJobsLetGoTogether, PlainArm) {
    auto const o = run_together(/*seeded=*/false, "TFS.15 plain arm");
    expect_together(o);
}

// Under ThreadSanitizer this arm reports its seeded pair when a second job exists for a call
// and the fixture does not order the two jobs. With no second job it writes nothing.
TEST(OffloadTwoJobsLetGoTogether, SeededArm) {
    auto const o = run_together(/*seeded=*/true, "TFS.15 seeded arm");
    expect_together(o);
}

// ── D-598-3, pin: granted, and the waiter not yet resumed (TFS.24) ───────────────────
//
// A FileStore over a plain pool, no recorder. A first store() is held inside its body by
// the offload probe; a second store(), bound to a signal of the cell's, queues at the
// store's lock. In the first input the coroutine that awaits the first call emits a
// cancellation with the terminal bit on the second call's slot as its next statement after
// the first call has returned: the first call's lock guard is a local of its body, released
// when it returns, so at the emit the lock has been granted to the second call and the
// second call's posted resumption has not run. In the second input the emit is made while
// the first call is still held. No mutant: the pin holds no statement of §11; the lock
// sequence's last reset is fixpp#583's.

struct PinHold {
    std::mutex m;
    std::condition_variable cv;
    bool armed = false;
    bool released = false;
    bool holding = false;
    int bodies = 0;
    bool timed_out = false;
};
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
PinHold g_pin;

void pin_probe(std::thread::id /*pool_thread*/) noexcept {
    std::unique_lock lk{g_pin.m};
    ++g_pin.bodies;
    if (!g_pin.armed) return;
    g_pin.armed = false;
    g_pin.holding = true;
    g_pin.cv.notify_all();
    if (!g_pin.cv.wait_for(lk, fe::kHoldBound, [] { return g_pin.released; })) {
        g_pin.timed_out = true;
    }
    g_pin.holding = false;
}

enum class PinEmit : std::uint8_t { after_the_grant, while_queued_at_the_lock };

struct PinOutcome {
    bool opened = false;
    bool first_held = false;
    bool second_queued = false;  // ready handlers run until none was left, not returned
    bool has_handler_at_emit = false;
    int bodies_at_emit = 0;  // the first call's body only, when the second's had not begun
    bool emitted = false;
    std::optional<fixpp::core::expected_t<void>> first;
    CallReading second;
    Retrieved second_record;
    bool timed_out = false;
};

PinOutcome run_pin(PinEmit where, char const* cell) {
    PinOutcome o;
    {
        std::lock_guard lk{g_pin.m};
        g_pin.armed = false;
        g_pin.released = false;
        g_pin.holding = false;
        g_pin.bodies = 0;
        g_pin.timed_out = false;
    }
    struct probe_scope {
        probe_scope() noexcept { fixpp::session::install_store_offload_probe(&pin_probe); }
        ~probe_scope() noexcept { fixpp::session::install_store_offload_probe(nullptr); }
        probe_scope(probe_scope const&) = delete;
        probe_scope& operator=(probe_scope const&) = delete;
    } const probe;
    struct temp_dir {
        std::filesystem::path path = fixpp::test_support::unique_temp_dir("b28_598_pin");
        temp_dir() = default;
        ~temp_dir() { (void)fixpp::test_support::try_remove_temp_dir(path); }
        temp_dir(temp_dir const&) = delete;
        temp_dir& operator=(temp_dir const&) = delete;
    } dir;
    asio::cancellation_signal sig;
    asio::thread_pool pool{2};
    struct release_on_exit {
        ~release_on_exit() {
            try {
                {
                    std::lock_guard lk{g_pin.m};
                    g_pin.released = true;
                }
                g_pin.cv.notify_all();
            } catch (...) {  // NOLINT(bugprone-empty-catch): nothing to report to from here
            }
        }
    } const release_guard;
    std::unique_ptr<FileStore> store;
    asio::io_context ioc;

    FileStore::Config c;
    c.directory = dir.path;
    c.sender_comp_id = kSender;
    c.target_comp_id = kTarget;
    c.max_frame_bytes = 4096;
    c.policy = FileStorePolicy{.which = FileStorePolicy::kind::commit_per_message};
    c.file_io_executor = pool.get_executor();
    {
        FileStoreFactory factory{c};
        auto ms = factory.make(kSender, kTarget, nullptr, std::size_t{1} << 20U, nullptr);
        if (!ms) return o;
        store.reset(static_cast<FileStore*>(ms->release()));
    }
    o.opened = true;
    auto const frame1 = make_frame(1);
    auto const frame2 = make_frame(2);
    auto const second = std::make_shared<CallReading>();

    {
        std::lock_guard lk{g_pin.m};
        g_pin.armed = true;
    }
    asio::co_spawn(
        ioc,
        [&]() -> asio::awaitable<void> {
            o.first =
                co_await store->store(1, std::span<const std::byte>{frame1}, direction_t::outbound);
            if (where == PinEmit::after_the_grant) {
                o.has_handler_at_emit = sig.slot().has_handler();
                {
                    std::lock_guard lk{g_pin.m};
                    o.bodies_at_emit = g_pin.bodies;
                }
                sig.emit(asio::cancellation_type::terminal);
                o.emitted = true;
            }
        },
        asio::detached);
    o.first_held = pump_until(ioc, [] {
        std::lock_guard lk{g_pin.m};
        return g_pin.holding;
    });
    if (o.first_held) {
        asio::co_spawn(ioc,
                       await_and_read<void>(
                           [&store, &frame2] {
                               return store->store(2, std::span<const std::byte>{frame2},
                                                   direction_t::outbound);
                           },
                           second),
                       asio::bind_cancellation_slot(sig.slot(), asio::detached));
        // (1) The second call is queued at the lock: the ready handlers run until none is
        // left, and it has not returned.
        ioc.poll();
        ioc.restart();
        o.second_queued = !second->done;
        if (where == PinEmit::while_queued_at_the_lock) {
            asio::post(ioc, [&] {
                o.has_handler_at_emit = sig.slot().has_handler();
                {
                    std::lock_guard lk{g_pin.m};
                    o.bodies_at_emit = g_pin.bodies;
                }
                sig.emit(asio::cancellation_type::terminal);
                o.emitted = true;
            });
            (void)pump_until(ioc, [&] { return o.emitted; });
        }
    }
    {
        std::lock_guard lk{g_pin.m};
        g_pin.released = true;
    }
    g_pin.cv.notify_all();
    (void)pump_until(ioc, [&] { return second->done && o.first.has_value() && o.emitted; });
    o.second = *second;
    {
        std::lock_guard lk{g_pin.m};
        o.timed_out = g_pin.timed_out;
    }
    if (o.second.done) {
        auto visitor = std::make_shared<CopyingVisitor>();
        std::optional<fixpp::core::expected_t<void>> r;
        asio::co_spawn(
            ioc,
            [&]() -> asio::awaitable<void> {
                r = co_await store->retrieve(2, 2, direction_t::outbound, *visitor);
            },
            asio::detached);
        (void)pump_until(ioc, [&] { return r.has_value(); });
        if (r) {
            o.second_record.ran = true;
            if (!r->has_value()) o.second_record.err = r->error();
            o.second_record.frames = visitor->frames;
        }
    }
    std::cout << "FS-RECORD " << cell << ": first_held=" << o.first_held
              << " second_queued=" << o.second_queued
              << " has_handler_at_emit=" << o.has_handler_at_emit
              << " bodies_at_emit=" << o.bodies_at_emit << " emitted=" << o.emitted << " first="
              << (!o.first               ? std::string{"not completed"}
                  : o.first->has_value() ? std::string{"success"}
                                         : error_text(o.first->error()))
              << " second=" << o.second.text() << " retrieve(2)=" << o.second_record.text() << "\n";
    return o;
}

TEST(LockGrantPin, GrantedAndNotYetResumed_TheCallCompletesAndNothingIsRecorded) {
    auto const o = run_pin(PinEmit::after_the_grant, "TFS.24 after the grant");
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.first_held && o.second_queued && !o.timed_out)
        << "the path was not exercised: first_held=" << o.first_held
        << " second_queued=" << o.second_queued << " timed_out=" << o.timed_out;
    ASSERT_TRUE(o.emitted) << "the path was not exercised: the emit was not made";
    // (2) The emit reached a handler.
    EXPECT_TRUE(o.has_handler_at_emit) << "the second call's slot had no handler at the emit";
    // The second call's body had not begun at the emit: only the first call's had.
    EXPECT_EQ(o.bodies_at_emit, 1) << "the second call's body had begun at the emit";
    EXPECT_TRUE(o.first.has_value() && o.first->has_value()) << "the first store() failed";
    EXPECT_TRUE(o.second.succeeded()) << "the second store() " << o.second.text();
    EXPECT_TRUE(o.second_record.exactly(2, make_frame(2)))
        << "the second call's record: " << o.second_record.text();
    // fixpp#583: the lock sequence's last reset replaces the thread's cancellation state, so a
    // cancellation emitted between the grant and the posted resumption is not recorded. That
    // issue's batch flips this assertion.
    EXPECT_EQ(o.second.after, asio::cancellation_type::none)
        << "the second call's thread reads a cancellation after the call: " << o.second.text();
}

// The same wiring, with the emit made while the second call is still queued at the lock.
TEST(LockGrantPin, EmittedWhileQueuedAtTheLock_ReturnsStoreCancelled) {
    auto const o = run_pin(PinEmit::while_queued_at_the_lock, "TFS.24 while queued");
    ASSERT_TRUE(o.opened) << "setup: the store did not open";
    ASSERT_TRUE(o.first_held && o.second_queued && !o.timed_out && o.emitted)
        << "the path was not exercised: first_held=" << o.first_held
        << " second_queued=" << o.second_queued << " timed_out=" << o.timed_out
        << " emitted=" << o.emitted;
    EXPECT_TRUE(o.has_handler_at_emit) << "the second call's slot had no handler at the emit";
    EXPECT_TRUE(o.second.returned && o.second.err == error::store_cancelled)
        << "the second store() " << o.second.text();
}

// ── D-598-1, D-598-3: a directly driven Session on the callback transport (TFS.12,
//    TFS.13, TFS.14) ────────────────────────────────────────────────────────────────
//
// An acceptor on a FileStore whose file executor is the delaying executor on a pool of two
// threads, or a pool of one thread held by a job of the cell (the control). The executor is
// armed from inside the Logout's toAdmin, after close(graceful)'s flush has been through the
// file executor, so the Logout's write job is job 1; for an application's send it is armed
// before the send.
//
// The release, on a Session. The cancellation reaches the store call through further
// handlers on the session's executor (the io_context the test thread runs), so the count of
// jobs is final only when those have run. The cell posts a marker to that executor and runs
// it until the marker has run, once per hop, and never runs it until it is idle: in the
// escalation input the terminal close posts itself again for as long as the graceful one
// runs (fixpp#593). The hops, re-derived from `git grep -n 'close_grace' --
// src/session/session.cpp` and asio's experimental/impl/parallel_group.hpp:
//   the grace timer input: close_grace's completion ends its arm of `||`; that arm's
//     completion reaches the parallel group, which emits the cancellation on the slot of the
//     logout phase's arm; that arm's co_spawn cancellation handler dispatches onto the
//     session's executor;
//   the escalation input: before those, the terminal close's own start, which cancels
//     close_grace through close_grace_wake_;
//   an application's send: the cell's emit, and Session::send's frames between the slot and
//     the store call.
// A marker beyond the hops costs nothing. What shows that the markers are enough is the run
// on the tree before the fix, where the count must read one more than the write's own.
//
// The grace timer input waits for close's own grace timer, which runs on real time; it is the
// one wait on a clock in these cells. From inside the Logout's toAdmin the cell reads the
// steady clock and arms a timer of its own on the session's executor at that reading plus
// the session's logout_disconnect_timeout_ms plus one millisecond. close set its deadline
// before it started the race, so before the toAdmin: the cell's deadline is strictly later.
// Both timers complete through the session's executor, so the cell's handler runs after
// close_grace's, and the markers follow it.
//
// No mutant for the control: on one thread the cancellation's job runs after the write's.

constexpr std::uint32_t kDirectLogoutTimeoutMs = 1000;
constexpr int kMarkerHops = 6;

struct LogoutToAdminApp final : fixpp::session::Application {
    std::function<void()> on_logout;
    void toAdmin(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& mv,
                 const fixpp::session::SessionId& /*id*/) override {
        if (on_logout && mv.msg_type() == "5") on_logout();
    }
};

enum class SessionInput : std::uint8_t { grace_timer, escalation, app_send };

// The MsgType(35) of each frame retrieve visits.
class TypeVisitor final : public fixpp::session::retrieve_visitor {
public:
    std::string types;
    asio::awaitable<fixpp::core::expected_t<fixpp::session::visit_result>> on_frame(
        seqnum_t /*seq*/, std::span<const std::byte> frame) noexcept override {
        std::string_view const text{reinterpret_cast<char const*>(frame.data()), frame.size()};
        types += (types.empty() ? "" : "+") + plain_rig::field_value(text, "35");
        co_return fixpp::session::visit_result::cont;
    }
};

struct StoreAt {
    bool read = false;
    std::optional<error> retrieve_error;
    std::string types;  // MsgType(35) of the frames at the number; empty: none
    std::optional<seqnum_t> next_out;
};

asio::awaitable<StoreAt> read_store_at(fixpp::session::MessageStore* store, seqnum_t seq) {
    StoreAt out;
    TypeVisitor visitor;
    auto const r = co_await store->retrieve(seq, seq, direction_t::outbound, visitor);
    if (!r) out.retrieve_error = r.error();
    out.types = visitor.types;
    if (auto const n = co_await store->next_seqnum(direction_t::outbound, false)) {
        out.next_out = *n;
    }
    out.read = true;
    co_return out;
}

struct SessionOutcome {
    bool active = false;
    bool armed = false;
    bool reached = false;
    bool before_deadline = true;  // the grace timer input: event 1 came before close's deadline
    bool cell_timer_seen = true;  // the grace timer input: the cell's own timer ran
    bool emitted = true;          // an application's send: the cell's emit ran
    int markers = 0;
    bool job1_began_at_count = false;
    int count = 0;
    bool others_ended = false;
    bool graceful_ready_before = false;
    bool terminal_ready_before = false;
    bool send_done_before = false;
    bool completed_after = false;
    std::optional<fixpp::core::expected_t<void>> graceful;
    std::optional<fixpp::core::expected_t<void>> terminal;
    CallReading send;
    seqnum_t seq = 0;  // the Logout's number, or the application message's
    StoreAt store;
    seqnum_t session_next_out = 0;
    std::size_t sink_frames_at_seq = 0;  // frames at `seq` the transport callback received
    int bodies = 0;
    int bodies_after_let_go = 0;
    bool timed_out = false;
    std::string events;

    [[nodiscard]] bool exercised(FileExec kind, SessionInput in) const {
        bool const common = active && armed && reached && emitted && markers == kMarkerHops &&
                            !job1_began_at_count && others_ended && !timed_out && completed_after;
        bool const timer_ok =
            in != SessionInput::grace_timer || (before_deadline && cell_timer_seen);
        (void)kind;
        return common && timer_ok;
    }
    [[nodiscard]] std::string text() const {
        return "active=" + std::to_string(active) + " armed=" + std::to_string(armed) +
               " reached=" + std::to_string(reached) +
               " before_deadline=" + std::to_string(before_deadline) +
               " cell_timer_seen=" + std::to_string(cell_timer_seen) +
               " emitted=" + std::to_string(emitted) + " markers=" + std::to_string(markers) +
               " job1_began_at_count=" + std::to_string(job1_began_at_count) +
               " count=" + std::to_string(count) + " others_ended=" + std::to_string(others_ended) +
               " timed_out=" + std::to_string(timed_out) +
               " completed_after=" + std::to_string(completed_after) +
               "\nthe executor's record:\n" + events;
    }
};

std::string result_text(std::optional<fixpp::core::expected_t<void>> const& r) {
    if (!r) return "did not complete";
    return r->has_value() ? std::string{"success"} : error_text(r->error());
}

void print_session_record(char const* cell, SessionOutcome const& o) {
    std::cout << "FS-RECORD " << cell << ": count=" << o.count << " markers=" << o.markers
              << " seq=" << o.seq << " graceful_before/after=" << o.graceful_ready_before << "/"
              << result_text(o.graceful) << " terminal_before/after=" << o.terminal_ready_before
              << "/" << result_text(o.terminal) << " send_before=" << o.send_done_before
              << " send=" << o.send.text() << " store_at_seq=["
              << (o.store.retrieve_error ? error_text(o.store.retrieve_error)
                                         : "35=" + o.store.types)
              << "] store_next_out=" << opt_text(o.store.next_out)
              << " session_next_out=" << o.session_next_out
              << " sink_frames_at_seq=" << o.sink_frames_at_seq << " bodies=" << o.bodies << "/"
              << o.bodies_after_let_go << "\n";
}

SessionOutcome run_direct_session(SessionInput in, FileExec kind, char const* cell) {
    using std::chrono::milliseconds;
    SessionOutcome o;
    bool const two = kind == FileExec::two_threads_write_delayed;
    // Declared first: the log and the probe outlive the pool and every job.
    auto const log = std::make_shared<fe::JobLog>();
    fe::scoped_body_probe const probe{*log};
    asio::thread_pool pool{two ? 2U : 1U};
    auto const app = std::make_shared<LogoutToAdminApp>();
    auto const sig = std::make_shared<asio::cancellation_signal>();
    DirectAcceptor f{DirectAcceptor::Options{
        .file_store = true,
        .logout_disconnect_timeout_ms = kDirectLogoutTimeoutMs,
        .application = app,
        .file_io_executor =
            asio::any_io_executor{fe::DelayingExecutor{pool.get_executor(), log, two}}}};
    // After the acceptor: a held thread is let go before the acceptor drains and joins.
    fe::let_go_on_exit const let_go{log};
    auto const payload = plain_rig::to_bytes(
        "35=D\x01"
        "11=ORD001\x01"
        "54=1\x01"
        "55=AAPL\x01");

    if (!f.open()) return o;
    auto const logon = f.logon();
    CallingThreadReadings logon_call;
    if (!f.feed_plain(logon, logon_call) || f.session->state() != fsm_state::Active) return o;
    o.active = true;
    auto* const store = session_test_access::store(*f.session);
    o.seq = session_test_access::seqnum_mgr(*f.session).peek_outbound();
    auto const timeout = milliseconds{f.cfg.logout_disconnect_timeout_ms};
    asio::steady_timer cell_timer{f.ioc};
    steady::time_point reading{};
    bool cell_timer_ran = false;
    auto const send = std::make_shared<CallReading>();
    std::future<fixpp::core::expected_t<void>> graceful_fut;
    std::future<fixpp::core::expected_t<void>> terminal_fut;

    if (in != SessionInput::app_send) {
        app->on_logout = [&] {
            if (o.armed) return;
            o.armed = true;
            reading = steady::now();
            log->arm();
            if (!two) log->hold_a_thread(pool);
            if (in == SessionInput::grace_timer) {
                cell_timer.expires_at(reading + timeout + milliseconds{1});
                cell_timer.async_wait([&](std::error_code const& ec) {
                    if (!ec) cell_timer_ran = true;
                });
            }
        };
        graceful_fut = f.spawn_close(close_mode::graceful);
    } else {
        log->arm();
        o.armed = true;
        if (!two) {
            log->hold_a_thread(pool);
            if (!log->wait_holders(1)) return o;
        }
        asio::co_spawn(
            f.ioc,
            await_and_read<void>(
                [&f, &payload] { return f.session->send(std::span<const std::byte>{payload}); },
                send),
            asio::bind_cancellation_slot(sig->slot(), asio::detached));
    }

    // Event 1.
    o.reached = f.run_until([&] { return two ? log->held() : log->handed() >= 1; }) &&
                (two || log->wait_holders(1));
    if (o.reached && in == SessionInput::grace_timer) {
        o.before_deadline = steady::now() < reading + timeout;
    }
    if (o.reached) {
        // Event 2.
        if (in == SessionInput::grace_timer) {
            o.cell_timer_seen = f.run_until([&] { return cell_timer_ran; }, timeout + kPumpBudget);
        } else if (in == SessionInput::escalation) {
            terminal_fut = f.spawn_close(close_mode::terminal);
        } else {
            o.emitted = false;
            asio::post(f.ioc, [&] {
                sig->emit(asio::cancellation_type::terminal);
                o.emitted = true;
            });
            (void)f.run_until([&] { return o.emitted; });
        }
        // The hops, one marker each.
        for (int i = 0; i < kMarkerHops; ++i) {
            bool seen = false;
            asio::post(f.ioc, [&seen] { seen = true; });
            if (f.run_until([&seen] { return seen; })) ++o.markers;
        }
        // Event 3.
        o.job1_began_at_count = log->job_began(1);
        o.count = log->handed();
        o.others_ended = two ? log->wait_others_ended() : true;
        o.graceful_ready_before = graceful_fut.valid() && DirectAcceptor::ready(graceful_fut);
        o.terminal_ready_before = terminal_fut.valid() && DirectAcceptor::ready(terminal_fut);
        o.send_done_before = send->done;
    }
    log->let_go();
    o.completed_after = f.run_until([&] {
        return (!graceful_fut.valid() || DirectAcceptor::ready(graceful_fut)) &&
               (!terminal_fut.valid() || DirectAcceptor::ready(terminal_fut)) &&
               (in != SessionInput::app_send || send->done);
    });
    app->on_logout = nullptr;
    if (graceful_fut.valid() && DirectAcceptor::ready(graceful_fut))
        o.graceful = graceful_fut.get();
    if (terminal_fut.valid() && DirectAcceptor::ready(terminal_fut))
        o.terminal = terminal_fut.get();
    o.send = *send;
    o.bodies = log->bodies();
    o.bodies_after_let_go = log->bodies_after_let_go();
    o.timed_out = log->held_timed_out();
    o.events = log->events();
    if (store != nullptr) {
        auto read_fut = asio::co_spawn(f.ioc, read_store_at(store, o.seq), asio::use_future);
        if (f.run_until([&] { return DirectAcceptor::ready(read_fut); })) o.store = read_fut.get();
    }
    o.session_next_out = session_test_access::seqnum_mgr(*f.session).peek_outbound();
    std::string const type = in == SessionInput::app_send ? "D" : "5";
    for (auto const& frame : f.sent) {
        if (plain_rig::field_value(frame, "35") == type &&
            plain_rig::field_value(frame, "34") == std::to_string(o.seq)) {
            ++o.sink_frames_at_seq;
        }
    }
    print_session_record(cell, o);
    return o;
}

void expect_direct_logout(SessionOutcome const& o, SessionInput in) {
    EXPECT_FALSE(o.graceful_ready_before) << "close(graceful) returned before the job was let go";
    if (in == SessionInput::escalation) {
        EXPECT_FALSE(o.terminal_ready_before)
            << "close(terminal) returned before the job was let go";
    }
    EXPECT_TRUE(o.store.read && !o.store.retrieve_error && o.store.types == "5")
        << "the Logout does not read back from the store: "
        << (o.store.retrieve_error ? error_text(o.store.retrieve_error) : "35=" + o.store.types);
    EXPECT_EQ(o.store.next_out, std::optional<seqnum_t>{o.seq + 1})
        << "the store's next outbound number";
    EXPECT_EQ(o.session_next_out, o.seq + 1) << "the session's next outbound number";
    // fixpp#584: the transmit step after a store call that completed with a cancellation
    // still recorded. That issue's batch flips this assertion.
    EXPECT_EQ(o.sink_frames_at_seq, 1U) << "the transport callback did not receive the Logout";
}

void expect_direct_send(SessionOutcome const& o) {
    EXPECT_FALSE(o.send_done_before) << "Session::send returned before the job was let go";
    EXPECT_TRUE(o.store.read && !o.store.retrieve_error && o.store.types == "D")
        << "the frame does not read back from the store: "
        << (o.store.retrieve_error ? error_text(o.store.retrieve_error) : "35=" + o.store.types);
    EXPECT_EQ(o.store.next_out, std::optional<seqnum_t>{o.seq + 1})
        << "the store's next outbound number";
    EXPECT_EQ(o.session_next_out, o.seq + 1) << "the session's next outbound number";
    // fixpp#584: the transmit step after a store call that completed with a cancellation
    // still recorded. That issue's batch flips these two assertions.
    EXPECT_EQ(o.sink_frames_at_seq, 1U) << "the transport callback did not receive the frame";
    EXPECT_TRUE(o.send.succeeded()) << "Session::send " << o.send.text();
}

void expect_session_gate(SessionOutcome const& o, FileExec kind, SessionInput in) {
    ASSERT_TRUE(o.exercised(kind, in)) << "the path was not exercised: " << o.text();
}

TEST(OffloadDirectSession, GracefulLogout_ByTheGraceTimer_IsWrittenAndSent) {
    auto const o =
        run_direct_session(SessionInput::grace_timer, FileExec::two_threads_write_delayed,
                           "TFS.12 callback, grace timer");
    expect_session_gate(o, FileExec::two_threads_write_delayed, SessionInput::grace_timer);
    expect_direct_logout(o, SessionInput::grace_timer);
    EXPECT_EQ(o.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(o.bodies_after_let_go, 1) << "the store's body did not begin after the release";
    EXPECT_EQ(o.count, 1) << "a job beyond the write's own was handed to the file executor";
}

TEST(OffloadDirectSession, GracefulLogout_ByAnEscalatingTerminalClose_IsWrittenAndSent) {
    auto const o = run_direct_session(SessionInput::escalation, FileExec::two_threads_write_delayed,
                                      "TFS.12 callback, escalation");
    expect_session_gate(o, FileExec::two_threads_write_delayed, SessionInput::escalation);
    expect_direct_logout(o, SessionInput::escalation);
    EXPECT_EQ(o.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(o.bodies_after_let_go, 1) << "the store's body did not begin after the release";
    EXPECT_EQ(o.count, 1) << "a job beyond the write's own was handed to the file executor";
}

TEST(OffloadDirectSession, ApplicationSend_IsStoredAndSent) {
    auto const o = run_direct_session(SessionInput::app_send, FileExec::two_threads_write_delayed,
                                      "TFS.13 callback");
    expect_session_gate(o, FileExec::two_threads_write_delayed, SessionInput::app_send);
    expect_direct_send(o);
    EXPECT_EQ(o.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(o.bodies_after_let_go, 1) << "the store's body did not begin after the release";
    EXPECT_EQ(o.count, 1) << "a job beyond the write's own was handed to the file executor";
}

TEST(OffloadDirectSession, Control_OneThread_GracefulLogout_ByTheGraceTimer) {
    auto const o =
        run_direct_session(SessionInput::grace_timer, FileExec::one_thread_held_by_the_cell,
                           "TFS.14 callback, grace timer");
    expect_session_gate(o, FileExec::one_thread_held_by_the_cell, SessionInput::grace_timer);
    expect_direct_logout(o, SessionInput::grace_timer);
    EXPECT_EQ(o.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(o.bodies_after_let_go, 1) << "the store's body did not begin after the release";
}

TEST(OffloadDirectSession, Control_OneThread_GracefulLogout_ByAnEscalatingTerminalClose) {
    auto const o =
        run_direct_session(SessionInput::escalation, FileExec::one_thread_held_by_the_cell,
                           "TFS.14 callback, escalation");
    expect_session_gate(o, FileExec::one_thread_held_by_the_cell, SessionInput::escalation);
    expect_direct_logout(o, SessionInput::escalation);
    EXPECT_EQ(o.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(o.bodies_after_let_go, 1) << "the store's body did not begin after the release";
}

TEST(OffloadDirectSession, Control_OneThread_ApplicationSend) {
    auto const o = run_direct_session(SessionInput::app_send, FileExec::one_thread_held_by_the_cell,
                                      "TFS.14 callback, application send");
    expect_session_gate(o, FileExec::one_thread_held_by_the_cell, SessionInput::app_send);
    expect_direct_send(o);
    EXPECT_EQ(o.bodies, 1) << "the store's body did not begin exactly once";
    EXPECT_EQ(o.bodies_after_let_go, 1) << "the store's body did not begin after the release";
}

}  // namespace
