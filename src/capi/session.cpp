// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/session.cpp — C-ABI session lifecycle / send / receive-callback
//                        registration (CA-005/006/007, T019/T020/T025/T031)
// [2i §4.2/§4.6/§4.9/§4.10] /
// specs/050-c-abi-session-send-recv/contracts/{lifecycle-surface,send-and-receive}.md.
//
// A fixpp_session_t is a NON-owning observer keyed by SessionId (storage owned by
// the engine). Every op does a SCOPED Engine::lookup() released before return —
// the handle NEVER caches a shared_ptr<Session> (lookup leases against
// Engine::lease_counter_, asserted zero by ~Engine in DEBUG).

#include "fix/c_api/session.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <stdexcept>
#include <utility>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include "capi_internal.hpp"

#include "fixpp/core/error.hpp"
#include "fixpp/session/session.hpp"  // Session::close/executor/is_open, close_mode

namespace {

using fixpp_capi::detail::translate;
using fixpp_capi::detail::translate_for_consumer;

// SC-006 steady-state-abort test seam: when set, fixpp_session_send throws inside
// its try block so the catch(...)→abort path (FR-008/FR-019) is witnessed. Always
// false in production — the setter is FIXPP_TEST_HOOKS-gated (capi_internal.hpp),
// so a production caller cannot flip it. Zero production overhead: one bool load.
bool g_send_throw_hook = false;

// Validate a non-owning session handle; resolves the owning engine. Returns the
// handle code (OK) or NULL/INVALID per the Feature-A discipline.
//
// GUARD ORDER is load-bearing (L-050-z split): check tag_==DEAD and
// state_==nullptr BEFORE dereferencing state_->engine_.  The retained shell is
// never freed (s_dead_shells keeps it reachable), so reading engine->tag_ /
// engine->state_ after engine destroy is safe — no UAF.  Only once we confirm
// state_ is non-null do we dereference state_->engine_.has_value().
fixpp_error_t check_session(const fixpp_session_t* s) noexcept {
    if (s == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (!s->valid.load(std::memory_order_acquire) || s->engine == nullptr ||
        s->engine->tag_ == FIXPP_HANDLE_TAG_DEAD || s->engine->state_ == nullptr ||
        !s->engine->state_->engine_.has_value()) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    return FIXPP_ERR_OK;
}

}  // namespace

extern "C" {

fixpp_error_t fixpp_session_open(fixpp_engine_t* engine, fixpp_session_config_t* cfg,
                                 fixpp_session_t** out_session) {
    if (out_session != nullptr) {
        *out_session = nullptr;
    }
    if (engine == nullptr || cfg == nullptr || out_session == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (engine->state_ == nullptr || !engine->state_->engine_.has_value()) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    // Register-before-start (FR-004): a session_open after engine_start is a
    // C-ABI-enforced config error (the registry is read on session strands
    // post-start without a mutex; a late register would race).
    if (engine->engine_started_) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }

    fixpp::session::SessionId id;
    try {
        id = fixpp::session::SessionId::from_config(cfg->cfg);
        // register_session takes SessionConfig BY VALUE → copies the builder's
        // config (builder still owns its copy; consumed/freed below on success).
        auto rr = engine->state_->engine_->register_session(cfg->cfg);
        if (!rr.has_value()) {
            // e.g. duplicate SessionId → session_invalid_argument (119) →
            // FIXPP_ERR_UNKNOWN (publication deferred, L-050-4). Builder NOT
            // consumed; caller still owns it.
            return translate_for_consumer(translate(rr.error()), engine->consumer_minor);
        }
    } catch (...) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // builder untouched on failure
    }

    try {
        // Slot for the trampoline (established flag + future recv callback);
        // pre-start, single-threaded → no race on the map.
        fixpp_capi::detail::SessionSlot& slot = engine->app_->slot_for(id);
        auto h = std::make_unique<fixpp_session>();
        h->engine = engine;
        h->id = id;
        h->slot = &slot;
        h->valid.store(true, std::memory_order_relaxed);  // single-threaded construction
        fixpp_session* raw = h.get();
        engine->sessions_.push_back(std::move(h));
        delete cfg;  // builder CONSUMED on success (invalidated)
        *out_session = raw;
        return FIXPP_ERR_OK;
    } catch (...) {
        // The session is registered with the engine but the handle alloc failed;
        // the builder is not consumed (caller frees). The registered session is
        // harmless (it will be driven/torn down at start/stop).
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
}

fixpp_error_t fixpp_session_is_established(fixpp_session_t* session, bool* out_established) {
    if (out_established == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    *out_established = false;
    if (fixpp_error_t c = check_session(session); c != FIXPP_ERR_OK) {
        return c;
    }
    // Lock-free atomic read of the genuine logged-on state (set by the
    // trampoline's onLogon/onLogout on the engine exec_). THREAD_SAFE, O(1).
    *out_established = session->slot->established.load(std::memory_order_acquire);
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_session_close(fixpp_session_t* session) {
    if (fixpp_error_t c = check_session(session); c != FIXPP_ERR_OK) {
        return c;
    }
    fixpp_engine* e = session->engine;
    fixpp_error_t code = FIXPP_ERR_OK;
    {
        // Scoped lease — released at the end of this block, before return.
        // state_ is guaranteed non-null here (check_session validated it above).
        std::shared_ptr<fixpp::session::Session> sess = e->state_->engine_->lookup(session->id);
        if (sess == nullptr) {
            // Registered but never established, or already gone → treat as an
            // already-closed lifecycle outcome (existing 049 code).
            code = FIXPP_ERR_THREAD_SESSION_LIFECYCLE;
        } else {
            // Post Session::close(graceful) onto the session's serialisation
            // domain and BLOCK on completion (FR-005; the SINGLE_THREAD
            // non-callback contract — a callback/strand caller would deadlock).
            // ASIO native cancellation inside close() shuts the transport so a
            // blocked idle read is broken (SC-007).
            try {
                // Spawn on the underlying strand (asio::any_io_executor), NOT on
                // the session_executor wrapper.  ASIO type-erases the executor arg
                // to asio::any_io_executor, which reference-counts the impl; the
                // session_executor wrapper's ~impl() is what TSan flagged when the
                // wrapper was type-erased across the caller thread and the worker
                // thread.  Using the long-lived underlying strand (the precedent at
                // engine.cpp:1567 / src/session/engine.cpp) avoids the cross-thread
                // wrapper destruction entirely — the strand's ref-counted impl is
                // long-lived (Session is alive for the duration of close's .get()).
                const asio::any_io_executor& close_exec = sess->executor().underlying();
                auto fut = asio::co_spawn(close_exec,
                                          sess->close(fixpp::session::close_mode::graceful),
                                          asio::use_future);
                fixpp::core::expected_t<void> r = fut.get();
                if (!r.has_value()) {
                    code = translate(r.error());
                }
            } catch (...) {
                code = FIXPP_ERR_THREAD_SESSION_LIFECYCLE;
            }
        }
    }
    // Publish the dead state with release semantics so any concurrent THREAD_SAFE
    // consumer (fixpp_session_send / fixpp_session_is_established) that acquires
    // `valid` after this point sees the handle as dead. close() itself is
    // SINGLE_THREAD, so the store races only the THREAD_SAFE callers (Q2 fix).
    session->valid.store(false, std::memory_order_release);
    // Success must NOT be routed through translate_for_consumer: OK is the
    // universal sentinel (not a minor-gated code), and the sibling ops
    // (open/send/is_established) return FIXPP_ERR_OK directly on success. Routing
    // OK through the downgrade would map a clean close to UNKNOWN for a
    // consumer_minor<2 engine — a misleading success→error flip. Downgrade error
    // codes only (forward-compat for the deferred session/app block, L-050-4).
    if (code == FIXPP_ERR_OK) {
        return FIXPP_ERR_OK;
    }
    return translate_for_consumer(code, e->consumer_minor);
}

fixpp_error_t fixpp_session_send(fixpp_session_t* session, const uint8_t* frame, size_t len) {
    // Steady-state thunk: an escaping C++ exception is an invariant violation →
    // fatal-log + abort, NOT translated (FR-008/SC-006).
    if (fixpp_error_t c = check_session(session); c != FIXPP_ERR_OK) {
        return c;
    }
    if (frame == nullptr || len == 0) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // not a committed wire frame
    }
    fixpp_engine* e = session->engine;
    try {
        if (g_send_throw_hook) {
            // SC-006 seam (test-only): force an escaping exception into the
            // catch(...)→abort below — the steady-state invariant-violation path.
            throw std::runtime_error("fixpp test seam: forced steady-state throw (SC-006)");
        }
        std::span<const std::byte> payload{reinterpret_cast<const std::byte*>(frame), len};
        // Engine::send is any-thread-safe (it enrols + exec-hops internally). The
        // borrowed `frame` outlives the call because .get() blocks until send
        // completes, and the span is copied by value into the coroutine frame.
        // state_ is guaranteed non-null here (check_session validated it above).
        auto fut = asio::co_spawn(e->state_->ioc_, e->state_->engine_->send(session->id, payload),
                                  asio::use_future);
        fixpp::core::expected_t<void> r = fut.get();
        if (!r.has_value()) {
            return translate_for_consumer(translate(r.error()), e->consumer_minor);
        }
        return FIXPP_ERR_OK;
    } catch (...) {
        std::fputs(
            "fixpp C-ABI: fixpp_session_send caught an escaping exception; aborting "
            "(steady-state invariant violation, FR-008)\n",
            stderr);
        std::abort();
    }
}

fixpp_error_t fixpp_session_register_callback(fixpp_session_t* session, fixpp_recv_cb cb,
                                              void* userdata) {
    if (fixpp_error_t c = check_session(session); c != FIXPP_ERR_OK) {
        return c;
    }
    if (session->slot == nullptr) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    // MUST precede fixpp_engine_start (FR-011): the trampoline map is read on the
    // session strand without a mutex, so a post-start registration would race
    // fromApp. Enforced, not merely documented (mirrors FR-004 for session_open).
    if (session->engine->engine_started_) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
    // Re-registration overwrites; cb == NULL clears. Single-threaded (pre-start).
    session->slot->cb = cb;
    session->slot->userdata = userdata;
    return FIXPP_ERR_OK;
}

}  // extern "C"

namespace fixpp_capi::detail {
// SC-006 send-throw seam setter. Compiled UNCONDITIONALLY (the library TU is built
// without FIXPP_TEST_HOOKS); only the declaration in capi_internal.hpp is gated, so
// a production caller cannot reach it (mirrors the file_store.cpp seam idiom).
void set_send_throw_hook(bool on) noexcept { g_send_throw_hook = on; }
}  // namespace fixpp_capi::detail
