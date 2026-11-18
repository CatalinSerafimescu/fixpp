// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/hooked_store.hpp — test-only.
//
// HookedStore: a MemoryStore behind a MessageStore that reports itself persistent, runs
// one-shot hooks at named store operations, and logs its counter writes into a StoreLog.
// Moved out of tests/session/test_session_plaintext_roundtrip.cpp (fixpp#518's
// LogonCloseDuringSuspension cells) by 093-inbound-frame-dispositions (tasks.md T074a), so
// a second executable can hold a store operation the same way.
//
// 104-sequence-reset-fidelity (tasks.md T015; research D10, its test-support additions) adds
// four readings a cell can take without a production seam:
//   - StoreLog::calls, an issue-time record of every reset() and reset_to call, taken at the
//     call's entry before any hook, hold or forward, in every reset_to_mode, with the
//     readings of StoreLog::call_snapshot, so a cell can tell the reset unit from the
//     teardown's reset;
//   - Hooks::on_inbound_persist_call, a per-call selector that holds a later inbound
//     persist, where Hooks::on_inbound_persist is one-shot;
//   - StoreLog::store_events, the two events of a held outbound store, each stamped with
//     the size of StoreLog::writes at its append, so its order against a counter write is
//     read from the stamps;
//   - blocking_write_state::starts (tests/support/blocking_write_transport.hpp), the write
//     count taken before the park.
// StoreLog::writes, the existing hooks, the holds and kHoldBound are unchanged.
#pragma once

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <fixpp/core/sync/async_mutex.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <fixpp/session/session_fsm.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fixpp::test_support {

// How a HookedStore answers MessageStore::reset_to (093 tasks.md T084):
//   forward      — forwards to the inner MemoryStore's reset_to (one operation), firing
//                  the on_reset hook and the hold there, as reset() does;
//   default_body — runs MessageStore's default body, so reset_to is this store's own
//                  reset() and next_seqnum(dir, true), each with its hooks;
//   contended    — fires on_reset, then waits on Hooks::reset_to_lock, an async_mutex the
//                  cell holds, as a store whose writer lock another holder has; once
//                  granted it forwards with the lock held, and releases it on return
//                  (093 plan OD-25's witness).
enum class reset_to_mode { forward, default_body, contended };

// Bound on a HookedStore hold. A cell that settles on a held operation needs a settle
// budget above it, so a hold that times out lets the cell settle and report
// `hold_timed_out` instead of a settle miss.
inline constexpr auto kHoldBound = std::chrono::seconds{1};

// What a cell reads at a reset() or reset_to call, through StoreLog::call_snapshot: the
// session's state and whether its transport is open (HookedStore cannot see the session).
struct CallReading {
    fixpp::session::fsm_state state{};
    bool transport_open = false;
};

// The counter writes a HookedStore completed, and the MemoryStore behind it, which
// outlives the session so a cell can read the final counters after stop().
struct StoreLog {
    struct Write {
        std::string op;  // "reset", "in+1", "out+1" or "reset_to <in> <out>"
        bool after_close_began = false;
    };
    std::function<bool()> close_began;
    std::vector<Write> writes;  // in completion order

    // One reset() or reset_to call, recorded at its entry (104). Under
    // reset_to_mode::default_body a reset_to's body issues this store's own reset(), which
    // is then recorded as a call of its own.
    enum class call_kind { reset, reset_to };
    struct Call {
        call_kind kind = call_kind::reset;
        // reset_to's targets; zero for a reset().
        fixpp::session::seqnum_t next_in = 0;
        fixpp::session::seqnum_t next_out = 0;
        // call_snapshot's readings at the call; empty when the cell set no snapshot.
        std::optional<CallReading> reading;
    };
    std::function<CallReading()> call_snapshot;
    std::vector<Call> calls;  // in issue order
    int reset_calls = 0;
    int reset_to_calls = 0;

    // A held outbound store's two events (104), appended by HookedStore::held_store only:
    // store_applied right after the inner store returns, store_returned right before the
    // held store returns. writes_at is writes.size() at the append, so an entry of writes
    // at an index below writes_at was completed before the event.
    enum class store_event_kind { store_applied, store_returned };
    struct StoreEvent {
        store_event_kind kind = store_event_kind::store_applied;
        fixpp::session::seqnum_t seq = 0;
        std::size_t writes_at = 0;
    };
    std::vector<StoreEvent> store_events;  // in append order
    int stores_made = 0;
    std::shared_ptr<fixpp::session::MemoryStore> inner;
    // reset() calls issued while close_began() was true.
    int resets_issued_after_close_began = 0;
    // A held operation waited out its bound without such a reset().
    bool hold_timed_out = false;
    // close(graceful)'s flush calls begun.
    int flushes_begun = 0;
    // A flush held by Hooks::flush_until waited out its bound.
    bool flush_hold_timed_out = false;

    void record(std::string op) {
        writes.push_back({.op = std::move(op), .after_close_began = close_began && close_began()});
    }
    void record_call(call_kind kind, fixpp::session::seqnum_t next_in,
                     fixpp::session::seqnum_t next_out) {
        calls.push_back({.kind = kind,
                         .next_in = next_in,
                         .next_out = next_out,
                         .reading = call_snapshot ? std::optional<CallReading>{call_snapshot()}
                                                  : std::nullopt});
        ++(kind == call_kind::reset ? reset_calls : reset_to_calls);
    }
    void record_store_event(store_event_kind kind, fixpp::session::seqnum_t seq) {
        store_events.push_back({.kind = kind, .seq = seq, .writes_at = writes.size()});
    }
};

// A MemoryStore that reports itself persistent, so the session hydrates from it and
// persists into it. Its outbound counter can start past 1, it can run a one-shot hook
// at named store operations, it logs its counter writes into a StoreLog, and it has a
// graceful-close flush. A hook stands in for a close() posted from another thread.
class HookedStore final : public fixpp::session::MessageStore {
public:
    struct Hooks {
        std::function<void()> on_hydrate;           // first inbound read
        std::function<void()> on_reset;             // first reset()
        std::function<void()> on_inbound_persist;   // first next_seqnum(inbound, true)
        std::function<void()> on_outbound_persist;  // first next_seqnum(outbound, true)
        // The operation whose hook fires completes, then does not return to the session
        // until a reset() has been issued after close() began, so the session resumes
        // with close()'s teardown reset already issued. Bounded by kHoldBound; past it
        // the log's hold_timed_out is set and the operation returns.
        bool hold_until_close_reset = false;
        // When set, the operation whose hook fires holds until this returns true, in
        // place of hold_until_close_reset's condition. Same bound.
        std::function<bool()> release_when;
        // Called on every outbound next_seqnum(_, false) and every outbound store(); when
        // one returns true, that operation completes and then holds, as
        // hold_until_close_reset does (release_when, else a reset after close() began).
        std::function<bool()> on_outbound_read;
        std::function<bool()> on_outbound_store;
        // Called on every next_seqnum(inbound, true), beside the one-shot
        // on_inbound_persist (104); when one returns true, that operation completes and
        // then holds, as on_outbound_store's does.
        std::function<bool()> on_inbound_persist_call;
        // A held operation (any of the above that holds) returns store_io_failure once
        // released, in place of its result, as a store whose I/O failed while the session
        // was suspended on it.
        bool fail_held = false;
        // reset_to_mode::contended's lock; the cell holds it while the unit waits.
        fixpp::sync::async_mutex* reset_to_lock = nullptr;
        // When set, close(graceful)'s flush yields the strand until this returns true,
        // in place of its fixed post sequence. Bounded by kHoldBound; past it the log's
        // flush_hold_timed_out is set and the flush returns.
        std::function<bool()> flush_until;
    };

    HookedStore(fixpp::session::seqnum_t outbound_next, Hooks hooks, std::shared_ptr<StoreLog> log,
                reset_to_mode mode = reset_to_mode::forward)
        : fixpp::session::MessageStore(flush_thunk_for<HookedStore>()),
          mode_(mode),
          hooks_(std::move(hooks)),
          log_(std::move(log)) {
        // A log that already holds a store (a cell's second connection) keeps it, and
        // its counters: the store is not seeded again.
        if (log_->inner) {
            inner_ = log_->inner;
            return;
        }
        inner_ = std::make_shared<fixpp::session::MemoryStore>(fixpp::session::MemoryStore::Config{
            .policy = fixpp::session::capacity_policy::unbounded});
        log_->inner = inner_;
        asio::io_context seed_ioc;
        asio::co_spawn(
            seed_ioc,
            [this, outbound_next]() -> asio::awaitable<void> {
                for (fixpp::session::seqnum_t s = 1; s < outbound_next; ++s) {
                    (void)co_await inner_->next_seqnum(fixpp::session::direction_t::outbound, true);
                }
            },
            asio::detached);
        seed_ioc.run();
    }

    asio::awaitable<fixpp::core::expected_t<void>> store(
        fixpp::session::seqnum_t seq, std::span<const std::byte> frame,
        fixpp::session::direction_t dir) noexcept override {
        if (dir == fixpp::session::direction_t::outbound && hooks_.on_outbound_store &&
            hooks_.on_outbound_store()) {
            return held_store(seq, frame, dir);
        }
        return inner_->store(seq, frame, dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        fixpp::session::seqnum_t begin, fixpp::session::seqnum_t end,
        fixpp::session::direction_t dir,
        fixpp::session::retrieve_visitor& visitor) noexcept override {
        return inner_->retrieve(begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> next_seqnum(
        fixpp::session::direction_t dir, bool increment) noexcept override {
        if (hooks_.on_hydrate && dir == fixpp::session::direction_t::inbound && !increment) {
            fire(hooks_.on_hydrate);
            // Answered without yielding, as a store holding the counter in memory may:
            // nothing has been stored inbound yet. The close the hook posted then runs at
            // the next read's leading post.
            return ready(fixpp::session::seqnum_min);
        }
        if (!increment && dir == fixpp::session::direction_t::outbound && hooks_.on_outbound_read &&
            hooks_.on_outbound_read()) {
            return held_read(dir);
        }
        if (!increment) return inner_->next_seqnum(dir, false);
        // The close a hook posts runs at the increment's leading post.
        const bool hooked =
            fire(dir == fixpp::session::direction_t::inbound ? hooks_.on_inbound_persist
                                                             : hooks_.on_outbound_persist);
        const bool selected = dir == fixpp::session::direction_t::inbound &&
                              hooks_.on_inbound_persist_call && hooks_.on_inbound_persist_call();
        return logged_increment(dir, (hooked && holds()) || selected);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        log_->record_call(StoreLog::call_kind::reset, 0, 0);
        if (log_->close_began && log_->close_began()) ++log_->resets_issued_after_close_began;
        // The close the hook posts runs at the reset's leading post.
        const bool hooked = fire(hooks_.on_reset);
        return logged_reset(hooked && holds());
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_to(
        fixpp::session::seqnum_t next_in, fixpp::session::seqnum_t next_out) noexcept override {
        log_->record_call(StoreLog::call_kind::reset_to, next_in, next_out);
        if (mode_ == reset_to_mode::default_body) {
            return fixpp::session::MessageStore::reset_to(next_in, next_out);
        }
        if (mode_ == reset_to_mode::contended) {
            (void)fire(hooks_.on_reset);
            return contended_reset_to(next_in, next_out);
        }
        // The close the hook posts runs at the inner reset_to's leading post.
        const bool hooked = fire(hooks_.on_reset);
        return logged_reset_to(next_in, next_out, hooked && holds());
    }

    // close(graceful) awaits this before it writes Disconnected. It yields the strand
    // a few times, as FileStore's flush does, so the session's other work can run
    // while close() is already under way; with Hooks::flush_until set, it yields until
    // that returns true or kHoldBound passes.
    asio::awaitable<fixpp::core::expected_t<void>> flush_for_session_close() {
        ++log_->flushes_begun;
        auto ex = co_await asio::this_coro::executor;
        if (hooks_.flush_until) {
            const auto deadline = std::chrono::steady_clock::now() + kHoldBound;
            while (!hooks_.flush_until()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    log_->flush_hold_timed_out = true;
                    break;
                }
                co_await asio::post(ex, asio::use_awaitable);
            }
            co_return fixpp::core::expected_t<void>{};
        }
        for (int i = 0; i < 8; ++i) {
            co_await asio::post(ex, asio::use_awaitable);
        }
        co_return fixpp::core::expected_t<void>{};
    }

private:
    static asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> ready(
        fixpp::session::seqnum_t v) {
        co_return v;
    }
    // True when a hook was set and has now run.
    static bool fire(std::function<void()>& hook) {
        if (!hook) return false;
        auto h = std::move(hook);
        hook = nullptr;
        h();
        return true;
    }
    [[nodiscard]] bool holds() const {
        return hooks_.hold_until_close_reset || static_cast<bool>(hooks_.release_when);
    }
    // Yields the strand until release_when() returns true, or without it until a
    // reset() is issued after close() began, or the bound passes.
    asio::awaitable<void> hold_until_close_reset() {
        auto ex = co_await asio::this_coro::executor;
        const auto deadline = std::chrono::steady_clock::now() + kHoldBound;
        auto released = [this] {
            return hooks_.release_when ? hooks_.release_when()
                                       : log_->resets_issued_after_close_began != 0;
        };
        while (!released()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                log_->hold_timed_out = true;
                co_return;
            }
            co_await asio::post(ex, asio::use_awaitable);
        }
    }
    // Logged when the write completes, not when it is issued: the MemoryStore applies
    // it only after its leading post and its mutex.
    asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> logged_increment(
        fixpp::session::direction_t dir, bool hold) {
        auto r = co_await inner_->next_seqnum(dir, true);
        log_->record(dir == fixpp::session::direction_t::inbound ? "in+1" : "out+1");
        if (hold) {
            co_await hold_until_close_reset();
            if (hooks_.fail_held) co_return std::unexpected(fixpp::core::error::store_io_failure);
        }
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> logged_reset_to(
        fixpp::session::seqnum_t next_in, fixpp::session::seqnum_t next_out, bool hold) {
        auto r = co_await inner_->reset_to(next_in, next_out);
        log_->record("reset_to " + std::to_string(next_in) + " " + std::to_string(next_out));
        if (hold) co_await hold_until_close_reset();
        co_return r;
    }
    // reset_to_mode::contended: waits for the cell's lock (async_lock enables total
    // cancellation while it waits), then forwards with it held.
    asio::awaitable<fixpp::core::expected_t<void>> contended_reset_to(
        fixpp::session::seqnum_t next_in, fixpp::session::seqnum_t next_out) {
        if (hooks_.reset_to_lock != nullptr) {
            auto granted = co_await hooks_.reset_to_lock->async_lock();
            if (!granted) {
                co_return std::unexpected(fixpp::core::error::store_cancelled);
            }
        }
        auto r = co_await inner_->reset_to(next_in, next_out);
        log_->record("reset_to " + std::to_string(next_in) + " " + std::to_string(next_out));
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> held_read(
        fixpp::session::direction_t dir) {
        auto r = co_await inner_->next_seqnum(dir, false);
        co_await hold_until_close_reset();
        if (hooks_.fail_held) co_return std::unexpected(fixpp::core::error::store_io_failure);
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> held_store(fixpp::session::seqnum_t seq,
                                                              std::span<const std::byte> frame,
                                                              fixpp::session::direction_t dir) {
        auto r = co_await inner_->store(seq, frame, dir);
        log_->record_store_event(StoreLog::store_event_kind::store_applied, seq);
        co_await hold_until_close_reset();
        log_->record_store_event(StoreLog::store_event_kind::store_returned, seq);
        if (hooks_.fail_held) co_return std::unexpected(fixpp::core::error::store_io_failure);
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> logged_reset(bool hold) {
        auto r = co_await (*inner_).reset();
        log_->record("reset");
        if (hold) co_await hold_until_close_reset();
        co_return r;
    }

    reset_to_mode mode_;
    std::shared_ptr<fixpp::session::MemoryStore> inner_;
    Hooks hooks_;
    std::shared_ptr<StoreLog> log_;
};

class HookedStoreFactory final : public fixpp::session::MessageStoreFactory {
public:
    fixpp::session::seqnum_t outbound_next = 1;
    HookedStore::Hooks hooks;  // moved into the first store made
    reset_to_mode mode = reset_to_mode::forward;
    std::shared_ptr<StoreLog> log = std::make_shared<StoreLog>();

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        ++log->stores_made;
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{
            std::make_unique<HookedStore>(outbound_next, std::move(hooks), log, mode)};
    }
};

}  // namespace fixpp::test_support
