// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/recording_store.hpp — test-only.
//
// RecordingStore: a MessageStore that forwards every call to a shipped store it owns
// and records, in one ordered log, when each call was made, when its coroutine body
// started, and what the inner store returned. RecordingStoreFactory makes one over a
// FileStore, or over the store another factory makes; a cell can choose what the factory
// reports as yields_persistent_store().
//
// fixpp#538 (.specify/536-537-538-recovery-liveness.md §5, "A cell that needs a store
// double"): a cell reads a shipped store's own result through this wrapper, so it
// measures that store's lookup and lock and not a double's. HookedStore
// (tests/support/hooked_store.hpp) is the precedent for a store behind a test factory.
//
// A call has two moments, and the log keeps them apart. `called_at` is taken in the
// call expression, by a plain function. `entered_at` is taken by the first statement of
// the coroutine body that forwards to the inner store. A caller's co_await can throw
// before that body starts (asio's await_transform tests the cancellation state first);
// such a call is recorded as called and never entered: it did not reach the store.
//
// A cell can also choose one call and have the wrapper's body, in place of forwarding
// it: throw, return a code, or wait until the cell releases it (StoreCallLog, "What the
// body does for one chosen call"). The call is recorded as entered in each case.
//
// A cell can also be told when a recorded call's result is in (StoreCallLog::on_return),
// and can have the wrapper keep one store() call's result from its caller until the cell
// releases it (StoreCallLog, "A store() call that returns to its caller late"): a store
// that returns a later store() call before an earlier one.
//
// StoreCallLog::reset_to_default_body makes the wrapper a store that does not override
// reset_to: MessageStore's default body runs, and the inner store sees a reset() and
// counter advances in place of its own reset_to.
//
// SeededMemoryStoreFactory is an inner factory for a store that already holds outbound
// frames when its session opens.
//
// Threading: the log is written on the session's serialisation domain and read by the
// test. It is for a session whose executor runs on the test's own thread
// (tests/session/plain_engine_rig.hpp); it takes no lock.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/memory_store.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "support/pump_until_ready.hpp"

namespace fixpp::test_support {

// The text of the exception a body throws for StoreCallLog::throw_in.
inline constexpr char const* kRecordingStoreThrown =
    "RecordingStore: the body throws for this call";

// Bound on StoreCallLog::hold_in. The hold is released by the cell, never by time; this
// only ends a hold a cell's own pumps have missed. A hold that reached it sets
// hold_timed_out(), and a cell that reads it fails. It is two bounded-pump budgets, so
// a cell takes no more than one bounded pump while a hold is in flight.
inline constexpr auto kBodyHoldBound = 2 * kPumpBudget;

enum class store_op : std::uint8_t { store, retrieve, next_seqnum, reset, reset_to };

// One call. `called_at`, `entered_at` and `returned_at` are positions in the log's own
// order (StoreCallLog::mark), so a cell can order a call against a moment of its own.
struct StoreCall {
    store_op op = store_op::store;
    fixpp::session::direction_t dir = fixpp::session::direction_t::inbound;  // not for reset*
    fixpp::session::seqnum_t first = 0;   // store: seq; retrieve: begin; reset_to: next_in
    fixpp::session::seqnum_t second = 0;  // retrieve: end; reset_to: next_out
    bool increment = false;               // next_seqnum
    std::size_t called_at = 0;
    std::size_t entered_at = 0;   // 0 until the forwarding body has started
    std::size_t returned_at = 0;  // 0 until the inner call has returned
    std::optional<fixpp::core::error> error = std::nullopt;  // the inner call's error
    fixpp::session::seqnum_t value = 0;  // next_seqnum's value, when it succeeded

    [[nodiscard]] bool entered() const noexcept { return entered_at != 0; }
    [[nodiscard]] bool returned() const noexcept { return returned_at != 0; }
};

class StoreCallLog {
public:
    // Recording starts here. A call made before arm() is forwarded and not recorded:
    // a session reads and writes its store while it opens and logs on, and a cell
    // that looks for one call must not find those.
    void arm() noexcept { armed_ = true; }

    // The next position in the log's order. A cell takes one to mark a moment of its
    // own ("before the stop", "before the release").
    [[nodiscard]] std::size_t mark() noexcept { return ++clock_; }

    [[nodiscard]] std::vector<StoreCall> const& calls() const noexcept { return calls_; }

    // The first recorded call made after position `after` that `pred` accepts, or
    // null. The pointer is valid until the next recorded call.
    template <class Pred>
    [[nodiscard]] StoreCall const* first_after(std::size_t after, Pred pred) const {
        for (auto const& c : calls_) {
            if (c.called_at > after && pred(c)) return &c;
        }
        return nullptr;
    }

    // Runs inside the call expression of every recorded call, after its `called_at`
    // is taken and before the caller's co_await of the returned awaitable. It is given
    // a copy, so it may itself cause recorded calls. It must not throw: the store's
    // members are noexcept.
    std::function<void(StoreCall const&)> on_call;

    // Runs when a recorded call's result is in: after the inner call has returned, or
    // the body has produced the code a cell chose, with `returned_at` and the result
    // recorded, and before the caller of the call resumes. It is given a copy. A cell
    // samples the session there ("when the store call returns"). It must not throw.
    std::function<void(StoreCall const&)> on_return;

    // ── What the body does for one chosen call ───────────────────────────────────────
    //
    // Each predicate is tested, in this order, against every recorded call when its body
    // starts; the first call one accepts takes that action, once, and the predicate is
    // cleared.
    //
    // throw_in: the body throws std::runtime_error{kRecordingStoreThrown} and the inner
    // store is not called.
    std::function<bool(StoreCall const&)> throw_in;
    // fail_in: the body returns fail_code and the inner store is not called.
    std::function<bool(StoreCall const&)> fail_in;
    fixpp::core::error fail_code = fixpp::core::error::store_io_failure;
    // hold_in: the body waits until release_hold(), yielding the session's executor,
    // and then returns held_result when that is set, or forwards the call. While a hold
    // is in flight the io_context always has a handler ready, so a cell must not run
    // it to exhaustion (io_context::poll()) then.
    std::function<bool(StoreCall const&)> hold_in;
    std::optional<fixpp::core::error> held_result;

    [[nodiscard]] bool holding() const noexcept { return holding_; }
    [[nodiscard]] bool hold_timed_out() const noexcept { return hold_timed_out_; }
    void release_hold() noexcept { hold_released_ = true; }

    // ── A store() call that returns to its caller late ───────────────────────────────
    //
    // late_return_in is tested against a recorded store() call when its inner call has
    // returned; the first call it accepts is forwarded and recorded as any other, and
    // the wrapper then keeps its result from the caller until release_late_return(),
    // yielding the executor, with the predicate cleared. A store() call made after it
    // can so return to its caller first. The bound is hold_in's, and a wait that reached
    // it sets late_return_timed_out().
    std::function<bool(StoreCall const&)> late_return_in;

    [[nodiscard]] bool returning_late() const noexcept { return returning_late_; }
    [[nodiscard]] bool late_return_timed_out() const noexcept { return late_return_timed_out_; }
    void release_late_return() noexcept { late_return_released_ = true; }

    // When set, the wrapper answers reset_to() with MessageStore's default body, as a
    // store that does not override it: this wrapper's own reset(), then its
    // next_seqnum(dir, true) for each target that is 2. Those calls are recorded and
    // forwarded; no reset_to reaches the inner store, and none is recorded.
    // HookedStore's reset_to_mode::default_body is the precedent.
    bool reset_to_default_body = false;

    int stores_made = 0;
    // The wrapper the factory made last, for a cell that reads the store itself. Null
    // before make() and once that wrapper is destroyed.
    fixpp::session::MessageStore* store = nullptr;

private:
    friend class RecordingStore;

    enum class body_action : std::uint8_t { forward, throw_exception, fail, hold };

    // The action for the call `id`, taken once. Not a coroutine: a call no predicate
    // accepts adds no await to its body.
    [[nodiscard]] body_action action_for(std::size_t id) {
        if (id == 0) return body_action::forward;
        auto const& c = calls_[id - 1];
        auto take = [&c](std::function<bool(StoreCall const&)>& pred) {
            if (!pred || !pred(c)) return false;
            pred = nullptr;
            return true;
        };
        if (take(throw_in)) return body_action::throw_exception;
        if (take(fail_in)) return body_action::fail;
        if (take(hold_in)) return body_action::hold;
        return body_action::forward;
    }

    // The hold: yields until the cell releases it or kBodyHoldBound has passed.
    asio::awaitable<void> wait_released() {
        auto const ex = co_await asio::this_coro::executor;
        auto const deadline = std::chrono::steady_clock::now() + kBodyHoldBound;
        holding_ = true;
        while (!hold_released_) {
            if (std::chrono::steady_clock::now() >= deadline) {
                hold_timed_out_ = true;
                break;
            }
            co_await asio::post(ex, asio::use_awaitable);
        }
        holding_ = false;
    }

    bool holding_ = false;
    bool hold_released_ = false;
    bool hold_timed_out_ = false;

    // Whether the call `id`, whose inner call has returned, is the one late_return_in
    // chooses. Taken once.
    [[nodiscard]] bool takes_late_return(std::size_t id) {
        if (id == 0 || !late_return_in || !late_return_in(calls_[id - 1])) return false;
        late_return_in = nullptr;
        return true;
    }

    // The wait of a late return: yields until the cell releases it or kBodyHoldBound has
    // passed.
    asio::awaitable<void> wait_late_return_released() {
        auto const ex = co_await asio::this_coro::executor;
        auto const deadline = std::chrono::steady_clock::now() + kBodyHoldBound;
        returning_late_ = true;
        while (!late_return_released_) {
            if (std::chrono::steady_clock::now() >= deadline) {
                late_return_timed_out_ = true;
                break;
            }
            co_await asio::post(ex, asio::use_awaitable);
        }
        returning_late_ = false;
    }

    bool returning_late_ = false;
    bool late_return_released_ = false;
    bool late_return_timed_out_ = false;

    // Returns the call's index plus one, or 0 when the log is not armed.
    [[nodiscard]] std::size_t call(StoreCall c) {
        if (!armed_) return 0;
        c.called_at = ++clock_;
        calls_.push_back(c);
        auto const id = calls_.size();
        if (on_call) on_call(c);
        return id;
    }
    void enter(std::size_t id) {
        if (id != 0) calls_[id - 1].entered_at = ++clock_;
    }
    void leave(std::size_t id, std::optional<fixpp::core::error> error,
               fixpp::session::seqnum_t value = 0) {
        if (id == 0) return;
        auto& c = calls_[id - 1];
        c.returned_at = ++clock_;
        c.error = error;
        c.value = value;
        if (on_return) {
            // A copy: the hook may itself cause recorded calls.
            auto const snapshot = c;
            on_return(snapshot);
        }
    }

    bool armed_ = false;
    std::size_t clock_ = 0;
    std::vector<StoreCall> calls_;
};

class RecordingStore final : public fixpp::session::MessageStore {
public:
    RecordingStore(std::unique_ptr<fixpp::session::MessageStore> inner,
                   std::shared_ptr<StoreCallLog> log)
        : fixpp::session::MessageStore(flush_thunk_for<RecordingStore>()),
          inner_(std::move(inner)),
          log_(std::move(log)) {
        log_->store = this;
    }
    ~RecordingStore() noexcept(false) override {
        if (log_->store == this) log_->store = nullptr;
    }

    // Each member records the call and returns the coroutine that forwards it.
    asio::awaitable<fixpp::core::expected_t<void>> store(
        fixpp::session::seqnum_t seq, std::span<const std::byte> frame,
        fixpp::session::direction_t dir) noexcept override {
        return store_body(log_->call({.op = store_op::store, .dir = dir, .first = seq}), seq, frame,
                          dir);
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        fixpp::session::seqnum_t begin, fixpp::session::seqnum_t end,
        fixpp::session::direction_t dir,
        fixpp::session::retrieve_visitor& visitor) noexcept override {
        return retrieve_body(
            log_->call({.op = store_op::retrieve, .dir = dir, .first = begin, .second = end}),
            begin, end, dir, visitor);
    }
    asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> next_seqnum(
        fixpp::session::direction_t dir, bool increment) noexcept override {
        return next_seqnum_body(
            log_->call({.op = store_op::next_seqnum, .dir = dir, .increment = increment}), dir,
            increment);
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept override {
        return reset_body(log_->call({.op = store_op::reset}));
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_to(
        fixpp::session::seqnum_t next_in, fixpp::session::seqnum_t next_out) noexcept override {
        if (log_->reset_to_default_body) {
            return fixpp::session::MessageStore::reset_to(next_in, next_out);
        }
        return reset_to_body(
            log_->call({.op = store_op::reset_to, .first = next_in, .second = next_out}), next_in,
            next_out);
    }

    // close(graceful)'s flush: the inner store's own, when it has one. Not recorded.
    asio::awaitable<fixpp::core::expected_t<void>> flush_for_session_close() {
        if (auto const hook = inner_->flush_hook(); hook != nullptr) {
            co_return co_await hook(*inner_);
        }
        co_return fixpp::core::expected_t<void>{};
    }

private:
    // In each body nothing is awaited between the inner call's return and leave(), so
    // `returned_at` orders the inner store's result and not a later resume.
    asio::awaitable<fixpp::core::expected_t<void>> store_body(std::size_t id,
                                                              fixpp::session::seqnum_t seq,
                                                              std::span<const std::byte> frame,
                                                              fixpp::session::direction_t dir) {
        log_->enter(id);
        if (auto const act = log_->action_for(id); act != body_action::forward) {
            if (auto const e = co_await not_forwarded(id, act)) co_return std::unexpected(*e);
        }
        auto r = co_await inner_->store(seq, frame, dir);
        log_->leave(id, error_of(r));
        if (log_->takes_late_return(id)) co_await log_->wait_late_return_released();
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve_body(
        std::size_t id, fixpp::session::seqnum_t begin, fixpp::session::seqnum_t end,
        fixpp::session::direction_t dir, fixpp::session::retrieve_visitor& visitor) {
        log_->enter(id);
        if (auto const act = log_->action_for(id); act != body_action::forward) {
            if (auto const e = co_await not_forwarded(id, act)) co_return std::unexpected(*e);
        }
        auto r = co_await inner_->retrieve(begin, end, dir, visitor);
        log_->leave(id, error_of(r));
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> next_seqnum_body(
        std::size_t id, fixpp::session::direction_t dir, bool increment) {
        log_->enter(id);
        if (auto const act = log_->action_for(id); act != body_action::forward) {
            if (auto const e = co_await not_forwarded(id, act)) co_return std::unexpected(*e);
        }
        auto r = co_await inner_->next_seqnum(dir, increment);
        log_->leave(id, error_of(r), r ? *r : fixpp::session::seqnum_t{0});
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_body(std::size_t id) {
        log_->enter(id);
        if (auto const act = log_->action_for(id); act != body_action::forward) {
            if (auto const e = co_await not_forwarded(id, act)) co_return std::unexpected(*e);
        }
        auto r = co_await inner_->reset();
        log_->leave(id, error_of(r));
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_to_body(
        std::size_t id, fixpp::session::seqnum_t next_in, fixpp::session::seqnum_t next_out) {
        log_->enter(id);
        if (auto const act = log_->action_for(id); act != body_action::forward) {
            if (auto const e = co_await not_forwarded(id, act)) co_return std::unexpected(*e);
        }
        auto r = co_await inner_->reset_to(next_in, next_out);
        log_->leave(id, error_of(r));
        co_return r;
    }

    using body_action = StoreCallLog::body_action;

    // The body of a call the cell chose (StoreCallLog, "What the body does for one
    // chosen call"). It throws; or it returns the code the call returns in place of
    // being forwarded, with the call recorded as returned; or, after a hold that ends
    // with no held_result, it returns nullopt and the caller forwards the call.
    asio::awaitable<std::optional<fixpp::core::error>> not_forwarded(std::size_t id,
                                                                     body_action act) {
        if (act == body_action::throw_exception) throw std::runtime_error{kRecordingStoreThrown};
        std::optional<fixpp::core::error> e;
        if (act == body_action::fail) {
            e = log_->fail_code;
        } else {
            co_await log_->wait_released();
            e = log_->held_result;
        }
        if (e) log_->leave(id, e);
        co_return e;
    }

    template <class T>
    [[nodiscard]] static std::optional<fixpp::core::error> error_of(
        fixpp::core::expected_t<T> const& r) {
        return r ? std::nullopt : std::optional<fixpp::core::error>{r.error()};
    }

    std::unique_ptr<fixpp::session::MessageStore> inner_;
    std::shared_ptr<StoreCallLog> log_;
};

// Makes a RecordingStore over the store an inner factory makes: the FileStore a
// FileStoreFactory makes from `cfg`, or whatever `inner` makes (a MemoryStoreFactory,
// for a volatile store). A cell gives a FileStore's `cfg` a file I/O executor of its
// own: the engine under plain_engine_rig.hpp has none.
class RecordingStoreFactory final : public fixpp::session::MessageStoreFactory {
public:
    explicit RecordingStoreFactory(fixpp::session::FileStore::Config cfg)
        : inner_factory_(std::make_shared<fixpp::session::FileStoreFactory>(std::move(cfg))) {}
    explicit RecordingStoreFactory(std::shared_ptr<fixpp::session::MessageStoreFactory> inner)
        : inner_factory_(std::move(inner)) {}

    std::shared_ptr<StoreCallLog> log = std::make_shared<StoreCallLog>();
    // What yields_persistent_store() reports, when a cell sets it before the session
    // opens; otherwise the inner factory's own answer.
    std::optional<bool> reports_persistent;

    [[nodiscard]] bool yields_persistent_store() const noexcept override {
        return reports_persistent.value_or(inner_factory_->yields_persistent_store());
    }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view sender, std::string_view target, std::pmr::memory_resource* mr,
        std::size_t max_store_memory_bytes,
        asio::any_io_executor file_io_executor) noexcept override {
        auto inner = inner_factory_->make(sender, target, mr, max_store_memory_bytes,
                                          std::move(file_io_executor));
        if (!inner) return std::unexpected(inner.error());
        ++log->stores_made;
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{
            std::make_unique<RecordingStore>(std::move(*inner), log)};
    }

private:
    std::shared_ptr<fixpp::session::MessageStoreFactory> inner_factory_;
};

// An inner factory for RecordingStoreFactory: the one store it makes is an unbounded
// MemoryStore that already holds `frames` outbound, stored in order from sequence
// number 1. It reports its store as persistent, so the session reads its sequence
// numbers from the store when it opens or logs on, and its next outbound number is the
// one after the last frame. A second make() fails: the frames went into the first store.
class SeededMemoryStoreFactory final : public fixpp::session::MessageStoreFactory {
public:
    explicit SeededMemoryStoreFactory(std::vector<std::string> frames)
        : frames_(std::move(frames)) {}

    [[nodiscard]] bool yields_persistent_store() const noexcept override { return true; }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view /*sender*/, std::string_view /*target*/, std::pmr::memory_resource* /*mr*/,
        std::size_t /*max_store_memory_bytes*/,
        asio::any_io_executor /*file_io_executor*/) noexcept override {
        if (made_) return std::unexpected(fixpp::core::error::store_factory_failed);
        made_ = true;
        auto store =
            std::make_unique<fixpp::session::MemoryStore>(fixpp::session::MemoryStore::Config{
                .policy = fixpp::session::capacity_policy::unbounded});
        bool stored = true;
        // The seeding runs on an io_context of its own, to completion, before the
        // session's executor ever sees the store.
        asio::io_context seed;
        asio::co_spawn(
            seed,
            [&]() -> asio::awaitable<void> {
                fixpp::session::seqnum_t seq = 0;
                for (auto const& text : frames_) {
                    auto const* const data = reinterpret_cast<std::byte const*>(text.data());
                    auto r =
                        co_await store->store(++seq, std::span<const std::byte>{data, text.size()},
                                              fixpp::session::direction_t::outbound);
                    stored = stored && r.has_value();
                }
            },
            asio::detached);
        seed.run();
        if (!stored) return std::unexpected(fixpp::core::error::store_factory_failed);
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{
            std::move(store)};
    }

private:
    std::vector<std::string> frames_;
    bool made_ = false;
};

}  // namespace fixpp::test_support
