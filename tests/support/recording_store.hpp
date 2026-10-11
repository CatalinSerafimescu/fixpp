// SPDX-License-Identifier: AGPL-3.0-or-later
//
// tests/support/recording_store.hpp — test-only.
//
// RecordingStore: a MessageStore that forwards every call to a shipped store it owns
// and records, in one ordered log, when each call was made, when its coroutine body
// started, and what the inner store returned. RecordingStoreFactory makes one over a
// FileStore.
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
// Threading: the log is written on the session's serialisation domain and read by the
// test. It is for a session whose executor runs on the test's own thread
// (tests/session/plain_engine_rig.hpp); it takes no lock.
#pragma once

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <cstddef>
#include <cstdint>
#include <fixpp/core/error.hpp>
#include <fixpp/session/direction.hpp>
#include <fixpp/session/file_store.hpp>
#include <fixpp/session/file_store_factory.hpp>
#include <fixpp/session/message_store.hpp>
#include <fixpp/session/message_store_factory.hpp>
#include <fixpp/session/retrieve_visitor.hpp>
#include <fixpp/session/seqnum.hpp>
#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace fixpp::test_support {

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

    int stores_made = 0;
    // The wrapper the factory made last, for a cell that reads the store itself. Null
    // before make() and once that wrapper is destroyed.
    fixpp::session::MessageStore* store = nullptr;

private:
    friend class RecordingStore;

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
        auto r = co_await inner_->store(seq, frame, dir);
        log_->leave(id, error_of(r));
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> retrieve_body(
        std::size_t id, fixpp::session::seqnum_t begin, fixpp::session::seqnum_t end,
        fixpp::session::direction_t dir, fixpp::session::retrieve_visitor& visitor) {
        log_->enter(id);
        auto r = co_await inner_->retrieve(begin, end, dir, visitor);
        log_->leave(id, error_of(r));
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<fixpp::session::seqnum_t>> next_seqnum_body(
        std::size_t id, fixpp::session::direction_t dir, bool increment) {
        log_->enter(id);
        auto r = co_await inner_->next_seqnum(dir, increment);
        log_->leave(id, error_of(r), r ? *r : fixpp::session::seqnum_t{0});
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_body(std::size_t id) {
        log_->enter(id);
        auto r = co_await inner_->reset();
        log_->leave(id, error_of(r));
        co_return r;
    }
    asio::awaitable<fixpp::core::expected_t<void>> reset_to_body(
        std::size_t id, fixpp::session::seqnum_t next_in, fixpp::session::seqnum_t next_out) {
        log_->enter(id);
        auto r = co_await inner_->reset_to(next_in, next_out);
        log_->leave(id, error_of(r));
        co_return r;
    }

    template <class T>
    [[nodiscard]] static std::optional<fixpp::core::error> error_of(
        fixpp::core::expected_t<T> const& r) {
        return r ? std::nullopt : std::optional<fixpp::core::error>{r.error()};
    }

    std::unique_ptr<fixpp::session::MessageStore> inner_;
    std::shared_ptr<StoreCallLog> log_;
};

// Makes a RecordingStore over the FileStore a FileStoreFactory makes from `cfg`. A
// cell gives `cfg` a file I/O executor of its own: the engine under
// plain_engine_rig.hpp has none.
class RecordingStoreFactory final : public fixpp::session::MessageStoreFactory {
public:
    explicit RecordingStoreFactory(fixpp::session::FileStore::Config cfg)
        : inner_factory_(std::move(cfg)) {}

    std::shared_ptr<StoreCallLog> log = std::make_shared<StoreCallLog>();

    [[nodiscard]] bool yields_persistent_store() const noexcept override {
        return inner_factory_.yields_persistent_store();
    }
    [[nodiscard]] fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>> make(
        std::string_view sender, std::string_view target, std::pmr::memory_resource* mr,
        std::size_t max_store_memory_bytes,
        asio::any_io_executor file_io_executor) noexcept override {
        auto inner = inner_factory_.make(sender, target, mr, max_store_memory_bytes,
                                         std::move(file_io_executor));
        if (!inner) return std::unexpected(inner.error());
        ++log->stores_made;
        return fixpp::core::expected_t<std::unique_ptr<fixpp::session::MessageStore>>{
            std::make_unique<RecordingStore>(std::move(*inner), log)};
    }

private:
    fixpp::session::FileStoreFactory inner_factory_;
};

}  // namespace fixpp::test_support
