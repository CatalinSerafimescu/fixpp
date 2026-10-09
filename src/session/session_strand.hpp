#pragma once
// SPDX-License-Identifier: AGPL-3.0-or-later
// src/session/session_strand.hpp — the session strand's construction (fixpp#544, B35;
// `.specify/544-hot-path-zero-alloc.md` §2.1, owner ruling R-2). Private: not installed
// (no install() rule names src/).
//
// WHY THE INNER EXECUTOR'S TYPE MATTERS. `asio::any_io_executor` stores its target inline
// only when the target fits its small-object buffer, in size and in alignment
// (`asio/execution/any_executor.hpp`, `object_type`); otherwise it allocates a
// `shared_target_executor`. A strand over a type-erased executor never fits, and its
// inner executor's `prefer(outstanding_work.tracked)` and `query(blocking)` then allocate
// on every handler. A strand over the concrete `io_context` executor fits on the Itanium
// ABI. Where it does not (MSVC applies EBO to the first empty base only, and
// `io_context::basic_executor_type` has two), the inner executor is `session_io_executor`
// below, which holds one word.
//
// THE FAST-PATH TEST is the EXACT target type `io_context::executor_type`. A work-tracked
// or custom-allocator io_context executor has a different target type and takes the
// fallback, a strand over the type-erased executor (B&L, fixpp#544).

#include <asio/any_io_executor.hpp>
#include <asio/execution.hpp>
#include <asio/io_context.hpp>
#include <asio/query.hpp>
#include <asio/require.hpp>
#include <asio/strand.hpp>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#if !defined(ASIO_HAS_DEDUCED_EXECUTE_MEMBER_TRAIT) || \
    !defined(ASIO_HAS_DEDUCED_QUERY_MEMBER_TRAIT) ||   \
    !defined(ASIO_HAS_DEDUCED_REQUIRE_MEMBER_TRAIT) || \
    !defined(ASIO_HAS_DEDUCED_EQUALITY_COMPARABLE_TRAIT)
// session_io_executor relies on asio deducing its members; without that asio needs
// explicit trait specialisations, which this header does not provide.
#error "session_strand.hpp: asio's deduced executor traits are unavailable on this compiler"
#endif

namespace fixpp::session::detail {

// True iff `asio::any_io_executor` stores an `E` inline: the size and alignment conditions
// of `any_executor_base`'s constructor, against its `object_type`
// (`aligned_storage<sizeof(shared_ptr<void>) + sizeof(void*), alignof(shared_ptr<void>)>`).
template <class E>
inline constexpr bool fits_any_io_executor_inline_v =
    sizeof(E) <= sizeof(std::shared_ptr<void>) + sizeof(void*) &&
    alignof(E) <= alignof(std::shared_ptr<void>);

// An executor over an `io_context`, one word wide: the `io_context*` with the property
// bits in its spare low bits. Every `require` returns this same type, so no property
// change produces a strand type that might not fit. `execute` forwards to a temporary
// `io_context::executor_type` carrying the same blocking and relationship bits; the
// tracked variant holds outstanding work through `on_work_started` / `on_work_finished`.
// Public asio API only, with no `asio::detail`.
//
// No base classes: an empty base can cost a word on MSVC, which is the layout this type
// exists to avoid.
class session_io_executor {
public:
    // Copies the source's blocking and relationship bits. `io_context::executor_type` is
    // never tracked, so the result is untracked.
    explicit session_io_executor(asio::io_context::executor_type const& ex) noexcept
        : session_io_executor{&asio::query(ex, asio::execution::context), bits_of(ex)} {}

    session_io_executor(session_io_executor const& other) noexcept : target_{other.target_} {
        if ((target_ & outstanding_work_tracked) != 0U) {
            if (auto* const ctx = context_ptr()) ctx->get_executor().on_work_started();
        }
    }

    // A moved-from tracked executor holds no work and no context, as asio's own
    // io_context executor does after a move.
    session_io_executor(session_io_executor&& other) noexcept : target_{other.target_} {
        if ((target_ & outstanding_work_tracked) != 0U) other.target_ = 0;
    }

    session_io_executor& operator=(session_io_executor const& other) noexcept {
        if (this != &other) {
            session_io_executor tmp{other};
            std::swap(target_, tmp.target_);
        }
        return *this;
    }

    session_io_executor& operator=(session_io_executor&& other) noexcept {
        if (this != &other) {
            session_io_executor tmp{std::move(other)};
            std::swap(target_, tmp.target_);
        }
        return *this;
    }

    ~session_io_executor() {
        if ((target_ & outstanding_work_tracked) != 0U) {
            if (auto* const ctx = context_ptr()) ctx->get_executor().on_work_finished();
        }
    }

    template <class Function>
    void execute(Function&& f) const {
        inner().execute(std::forward<Function>(f));
    }

    [[nodiscard]] asio::io_context& query(asio::execution::context_t) const noexcept {
        return *context_ptr();
    }

    [[nodiscard]] asio::execution::blocking_t query(asio::execution::blocking_t) const noexcept {
        return (bits() & blocking_never) != 0U
                   ? asio::execution::blocking_t(asio::execution::blocking.never)
                   : asio::execution::blocking_t(asio::execution::blocking.possibly);
    }

    [[nodiscard]] asio::execution::relationship_t query(
        asio::execution::relationship_t) const noexcept {
        return (bits() & relationship_continuation) != 0U
                   ? asio::execution::relationship_t(asio::execution::relationship.continuation)
                   : asio::execution::relationship_t(asio::execution::relationship.fork);
    }

    [[nodiscard]] asio::execution::outstanding_work_t query(
        asio::execution::outstanding_work_t) const noexcept {
        return (bits() & outstanding_work_tracked) != 0U
                   ? asio::execution::outstanding_work_t(asio::execution::outstanding_work.tracked)
                   : asio::execution::outstanding_work_t(
                         asio::execution::outstanding_work.untracked);
    }

    [[nodiscard]] session_io_executor require(asio::execution::blocking_t::never_t) const noexcept {
        return {context_ptr(), bits() | blocking_never};
    }
    [[nodiscard]] session_io_executor require(
        asio::execution::blocking_t::possibly_t) const noexcept {
        return {context_ptr(), bits() & ~blocking_never};
    }
    [[nodiscard]] session_io_executor require(
        asio::execution::relationship_t::continuation_t) const noexcept {
        return {context_ptr(), bits() | relationship_continuation};
    }
    [[nodiscard]] session_io_executor require(
        asio::execution::relationship_t::fork_t) const noexcept {
        return {context_ptr(), bits() & ~relationship_continuation};
    }
    [[nodiscard]] session_io_executor require(
        asio::execution::outstanding_work_t::tracked_t) const noexcept {
        return {context_ptr(), bits() | outstanding_work_tracked};
    }
    [[nodiscard]] session_io_executor require(
        asio::execution::outstanding_work_t::untracked_t) const noexcept {
        return {context_ptr(), bits() & ~outstanding_work_tracked};
    }

    friend bool operator==(session_io_executor const& a, session_io_executor const& b) noexcept {
        return a.target_ == b.target_;
    }
    friend bool operator!=(session_io_executor const& a, session_io_executor const& b) noexcept {
        return a.target_ != b.target_;
    }

private:
    static constexpr std::uintptr_t blocking_never = 1;
    static constexpr std::uintptr_t relationship_continuation = 2;
    static constexpr std::uintptr_t outstanding_work_tracked = 4;
    static constexpr std::uintptr_t runtime_bits = 7;
    static_assert(alignof(asio::io_context) > runtime_bits,
                  "the property bits need three spare low bits in an io_context*");

    session_io_executor(asio::io_context* ctx, std::uintptr_t bits) noexcept
        : target_{reinterpret_cast<std::uintptr_t>(ctx) | bits} {
        if ((bits & outstanding_work_tracked) != 0U && ctx != nullptr) {
            ctx->get_executor().on_work_started();
        }
    }

    static std::uintptr_t bits_of(asio::io_context::executor_type const& ex) noexcept {
        std::uintptr_t b = 0;
        if (asio::query(ex, asio::execution::blocking) == asio::execution::blocking.never) {
            b |= blocking_never;
        }
        if (asio::query(ex, asio::execution::relationship) ==
            asio::execution::relationship.continuation) {
            b |= relationship_continuation;
        }
        return b;
    }

    [[nodiscard]] asio::io_context* context_ptr() const noexcept {
        return reinterpret_cast<asio::io_context*>(target_ & ~runtime_bits);
    }
    [[nodiscard]] std::uintptr_t bits() const noexcept { return target_ & runtime_bits; }

    // The untracked io_context executor with this executor's blocking and relationship
    // bits. Tracking is held by this object, so the temporary never counts work.
    [[nodiscard]] asio::io_context::executor_type inner() const noexcept {
        auto ex = context_ptr()->get_executor();
        if ((bits() & blocking_never) != 0U) {
            ex = asio::require(ex, asio::execution::blocking.never);
        }
        if ((bits() & relationship_continuation) != 0U) {
            ex = asio::require(ex, asio::execution::relationship.continuation);
        }
        return ex;
    }

    std::uintptr_t target_;
};

// The session strand's inner executor on the fast path: `io_context::executor_type` where
// its strand fits `any_io_executor` inline, `session_io_executor` where it does not.
using session_inner_executor_t =
    std::conditional_t<fits_any_io_executor_inline_v<asio::strand<asio::io_context::executor_type>>,
                       asio::io_context::executor_type, session_io_executor>;

// The one construction point of a session strand. If `exec`'s target is exactly
// `io_context::executor_type`, returns a strand over `session_inner_executor_t`;
// otherwise a strand over `exec` itself.
[[nodiscard]] asio::any_io_executor make_session_strand(asio::any_io_executor const& exec);

}  // namespace fixpp::session::detail
