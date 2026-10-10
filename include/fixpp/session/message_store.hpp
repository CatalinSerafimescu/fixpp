// SPDX-License-Identifier: AGPL-3.0-or-later
//
// include/fixpp/session/message_store.hpp
//
// fixpp::session::MessageStore — a pure-virtual plugin interface ([const §XIV.1]
// row 5). Every pure virtual counts against [const §XIV.2]'s cap of five; re-derive
// the count with `grep -cE '^[^/]*\)[^;]*= 0;' include/fixpp/session/message_store.hpp`.
// 093-inbound-frame-dispositions adds reset_to (data-model E-9), a non-pure virtual
// with a default body, so it is not counted.
//
// Anchor: .specify/2e-msgstore.md v0.5 §4.1 / §4.1.1 / §4.1.2. Entity E1.
// FR-001 (interface), FR-028 (engine-internal graceful-close hook
// dispatch mechanism A1). I-01 / I-02 / I-03 / I-04 / I-05 / I-17 / I-18.
//
// NO public flush() — N2 close (the public surface is store/retrieve/
// next_seqnum/reset only). NO store_concurrent_writer variant — Codex P1-5
// close (FIFO-fair async_mutex makes it impossible). All four methods
// acquire the per-instance fixpp::sync::async_mutex on entry (I-01).
//
// Mirror of specs/008-message-store/contracts/message_store.hpp. The
// contract is silent on the engine-internal hook scaffolding around the
// public pure-virtual interface; the scaffolding below is the A1
// factory-type-tag retention mechanism — the concept-shaped non-virtual
// dispatch path for FR-028 / I-17 (Opus N3-P2-1 close). NO RTTI, NO
// dynamic_cast, NO extra pure-virtual.
#pragma once

#include <asio/awaitable.hpp>
#include <cstddef>
#include <fixpp/core/detail/asio_recycler_config.hpp>
#include <fixpp/core/error.hpp>                                  // expected_t / error
#include <fixpp/session/detail/has_flush_for_session_close.hpp>  // concept (FR-028 A1)
#include <fixpp/session/direction.hpp>                           // direction_t
#include <fixpp/session/seqnum.hpp>                              // seqnum_t
#include <span>

namespace fixpp::session {

class retrieve_visitor;  // forward-decl; full type in retrieve_visitor.hpp

namespace detail {
// reset_to's precondition (093-inbound-frame-dispositions, data-model E-9): next_in and
// next_out are each 1 or 2.
[[nodiscard]] constexpr bool reset_to_targets_valid(seqnum_t next_in, seqnum_t next_out) noexcept {
    auto const ok = [](seqnum_t v) { return v == seqnum_min || v == seqnum_min + 1; };
    return ok(next_in) && ok(next_out);
}
}  // namespace detail

class MessageStore {
public:
    // ── A1 hook scaffolding (engine-internal; FR-028 / I-17) ────────────
    //
    // flush_hook_fn: type-erased graceful-close hook. The engine reads it
    // ONCE at session open via flush_hook() and dispatches via one
    // indirect non-virtual call through the stashed pointer. NO RTTI.
    // Signature mirrors a typed FileStore::flush_for_session_close() so
    // the typed thunk's body is a static_cast + member-call, never a
    // type-id dispatch.
    using flush_hook_fn =
        asio::awaitable<fixpp::core::expected_t<void>> (*)(MessageStore&) noexcept;

    // flush_thunk_for<Self>(): factory-type-tag selector. The concrete
    // store's CTOR passes flush_thunk_for<ConcreteStore>() through to the
    // MessageStore base ctor; the concept gate decides whether a typed
    // thunk or nullptr is returned at compile time (T031's named concept
    // is the canonical predicate per FR-028).
    template <class Self>
    [[nodiscard]] static constexpr flush_hook_fn flush_thunk_for() noexcept {
        if constexpr (fixpp::session::detail::has_flush_for_session_close<Self>) {
            return +[](MessageStore& base) noexcept
                       -> asio::awaitable<fixpp::core::expected_t<void>> {
                return static_cast<Self&>(base).flush_for_session_close();
            };
        } else {
            return nullptr;
        }
    }

    // flush_hook(): engine reads ONCE at session open, stashes the
    // returned pointer on the Session, and (under Session::close(graceful)
    // only) `co_await (*hook)(*store_)`. Under Session::close(terminal)
    // the hook is skipped per Appendix D §D.2.
    [[nodiscard]] flush_hook_fn flush_hook() const noexcept { return flush_hook_; }

protected:
    // Protected to keep concrete-impl-only constructibility (no
    // user-instantiable abstract bases). Each concrete store passes
    // flush_thunk_for<Self>() so the A1 mechanism is single-source-of-truth.
    explicit MessageStore(flush_hook_fn hook = nullptr) noexcept : flush_hook_(hook) {}

public:
    MessageStore(const MessageStore&) = delete;
    MessageStore& operator=(const MessageStore&) = delete;
    MessageStore(MessageStore&&) = delete;
    MessageStore& operator=(MessageStore&&) = delete;
    // noexcept(false): MemoryStore holds async_mutex whose destructor is
    // noexcept(false) ([2f §4.1] — fires std::terminate if held at destruction).
    // Derived stores inherit this exception specification; callers must drain
    // the mutex via cancel_and_drain() before destroying the store.
    virtual ~MessageStore() noexcept(false) = default;

    // ── Public pure-virtual interface ([const §XIV.2]: see the file header) ──

    // store: persist a single frame. frame is taken as a non-owning span and
    // MUST be deep-copied into store-owned storage AFTER acquiring the writer
    // mutex and BEFORE any further suspension that could release the session
    // strand (i.e., before the nested co_spawn that offloads pwrite /
    // fdatasync to file_io_executor per 035-filestore-io-offload) per
    // [2b §6.4] view-escape and design-doc §6.3.3 step 3 (I-02; under the
    // v1.0 single-session-serialisation-domain discipline the uncontended
    // async_mutex::async_lock() does NOT suspend per [2f §4.3.2] fast-path).
    // On mutex-acquire, verifies seq == next_seqnum(dir, false) inside the
    // critical section; mismatch → store_seqnum_out_of_order (I-05). Under
    // capacity_policy::bounded, MemoryStore returns store_capacity_exhausted
    // when the per-direction cap is reached (I-08). FileStore returns
    // store_io_failure on disk-side faults.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>> store(
        seqnum_t seq, std::span<const std::byte> frame [[clang::lifetimebound]],
        direction_t dir) noexcept = 0;

    // retrieve: walk [begin, end] (end == 0 → infinity / to current tail).
    // Acquires the writer mutex to validate begin/end and snapshot the index,
    // releases the mutex BEFORE invoking visitor.on_frame's co_await (I-03).
    // begin == 0 → store_seqnum_invalid; end != 0 && end < begin →
    // store_invalid_range; gap → store_seqnum_gap (I-19). Mid-traversal
    // mutation is detected and the next visitor call observes the new state
    // without UB; already-visited frames are not re-visited; iteration stops
    // at the original end.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>> retrieve(
        seqnum_t begin, seqnum_t end, direction_t dir,
        retrieve_visitor& visitor [[clang::lifetimebound]]) noexcept = 0;

    // next_seqnum: read (increment=false) or read-then-increment
    // (increment=true). The increment is serialised on the writer mutex
    // (Opus N2-P2-2; the v0.2 atomic-fetch-add wording was retired).
    // Overflow on increment when current == seqnum_max → store_seqnum_overflow
    // and session-fatal; the store does NOT autonomously reset (I-18).
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<seqnum_t>> next_seqnum(
        direction_t dir, bool increment) noexcept = 0;

    // reset: clear all frames (both directions) and rewind counters to
    // next_inbound = next_outbound = 1. FileStore: atomic at the rename of
    // <live>.log.reset.tmp PLUS the platform durability primitive (Linux:
    // parent-dir fsync MANDATORY; Windows: a POSIX-semantics
    // SetFileInformationByHandle rename, then FlushFileBuffers on the renamed
    // file's handle, MANDATORY; I-15). If that primitive or anything after the
    // rename fails, reset returns store_io_failure and the store refuses every
    // later operation until it is reopened (L-035-2). MemoryStore: entry-array
    // zero pass under writer mutex.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>> reset() noexcept = 0;

    // reset_to (093-inbound-frame-dispositions, data-model E-9; fixpp#524): clear all
    // frames, as reset() does, and leave next_inbound = next_in and
    // next_outbound = next_out. The session calls it from its 141=Y reset unit.
    // Precondition: next_in and next_out are each 1 or 2; any other value returns
    // session_invalid_argument and changes nothing.
    //
    // The default body runs reset(), then next_seqnum(dir, true) once for each target
    // that is 2, and returns the first error, at which it stops. It is not atomic: a
    // crash between those steps can leave the intermediate state. Override it to make
    // the operation one step under the store's writer lock, so no reader and no
    // restart sees an intermediate state (re-derive which stores do with
    // `git grep -n "reset_to(" -- include src`). An override
    // that holds a FIFO writer lock across the whole operation also keeps the (1, 1)
    // outcome when Session::close()'s bounded wait for the unit expires, because the
    // teardown reset queues behind it.
    //
    // Adding this virtual changes MessageStore's vtable: C++ code built against the
    // header without it must be rebuilt. A subclass needs no source change.
    [[nodiscard]] virtual asio::awaitable<fixpp::core::expected_t<void>> reset_to(
        seqnum_t next_in, seqnum_t next_out) noexcept {
        if (!detail::reset_to_targets_valid(next_in, next_out)) {
            co_return std::unexpected(fixpp::core::error::session_invalid_argument);
        }
        if (auto r = co_await reset(); !r) {
            co_return r;
        }
        if (next_in == seqnum_min + 1) {
            if (auto r = co_await next_seqnum(direction_t::inbound, true); !r) {
                co_return std::unexpected(r.error());
            }
        }
        if (next_out == seqnum_min + 1) {
            if (auto r = co_await next_seqnum(direction_t::outbound, true); !r) {
                co_return std::unexpected(r.error());
            }
        }
        co_return fixpp::core::expected_t<void>{};
    }

private:
    flush_hook_fn flush_hook_{nullptr};  // A1 stash; set once by ctor, read by flush_hook()
};

}  // namespace fixpp::session
