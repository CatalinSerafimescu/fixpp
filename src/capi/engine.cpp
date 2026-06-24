// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/engine.cpp — C-ABI engine lifecycle + the CapiApplication trampoline
//                       (CA-005 / CA-007, T013/T017/T018/T032)
// [2i §4.2/§4.10] / specs/050-c-abi-session-send-recv/contracts/{lifecycle-surface,
// send-and-receive}.md.
//
// The structural novelty: the C++ Engine owns NO worker threads (engine.hpp:222);
// the C-ABI boundary owns an internal io_context + worker thread(s) + a work-guard
// (research D-2). Lifecycle = register-then-start-once:
//   create → session_open ×N (= register_session, pre-start) → start (= Engine::start)
//          → drive → session_close → destroy (= co_await stop() + join + ~Engine).
//
// THREAD: all symbols here are SINGLE_THREAD. create/start are construction-time
// thunks (catch → *_CONFIG); destroy is idempotent, NULL-safe, never-throws.

#include "fix/c_api/engine.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

#include <asio/co_spawn.hpp>
#include <asio/use_future.hpp>

#include "capi_internal.hpp"

#include "fix/c_api/version.h"  // FIXPP_C_ABI_VERSION_MAJOR
#include "fixpp/core/system_clock_source.hpp"

// ── CapiApplication trampoline (E-5) ────────────────────────────────────────

namespace fixpp_capi::detail {

SessionSlot& CapiApplication::slot_for(const fixpp::session::SessionId& id) {
    auto it = slots_.find(id);
    if (it == slots_.end()) {
        it = slots_.emplace(id, std::make_unique<SessionSlot>()).first;
    }
    return *it->second;
}

SessionSlot* CapiApplication::find_(const fixpp::session::SessionId& id) noexcept {
    auto it = slots_.find(id);
    return it == slots_.end() ? nullptr : it->second.get();
}

// onLogon/onLogout run on the engine exec_ (single-thread confinement); publish
// the logged-on state to the per-session atomic that fixpp_session_is_established
// reads from any thread. slots_ is immutable post-start so find_ is race-free.
void CapiApplication::onLogon(const fixpp::session::SessionId& id) {
    if (auto* s = find_(id)) {
        s->established.store(true, std::memory_order_release);
    }
}

void CapiApplication::onLogout(const fixpp::session::SessionId& id) {
    if (auto* s = find_(id)) {
        s->established.store(false, std::memory_order_release);
    }
}

// fromApp runs ON the session strand (steady-state hot path). Wrap the borrowed
// MessageView in a STACK fixpp_msg (no heap — [const §VIII.5], D-6) and invoke
// the registered C callback. An exception escaping the C callback is an on-strand
// steady-state invariant violation → fatal-log + abort (matches the send thunk,
// FR-008); it is NOT routed into fromApp's expected_t reject path. v1.0 callback
// is void → always accept ({}). [send-and-receive.md "Receive (CA-007)"]
fixpp::core::expected_t<void> CapiApplication::fromApp(
    const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
    const fixpp::session::SessionId& id) {
    SessionSlot* s = find_(id);
    if (s == nullptr || s->cb == nullptr) {
        return {};  // no callback registered → accept
    }
    fixpp_msg inbound{};  // stack wrapper; valid only for this dispatch window
    inbound.view = &msg;
    try {
        s->cb(&inbound, s->userdata);
    } catch (...) {
        std::fputs(
            "fixpp C-ABI: receive callback threw; aborting (on-strand steady-state "
            "invariant violation, FR-008)\n",
            stderr);
        std::abort();
    }
    return {};
}

}  // namespace fixpp_capi::detail

// ── Engine lifecycle ────────────────────────────────────────────────────────

extern "C" {

fixpp_error_t fixpp_engine_create(fixpp_engine_config_t* cfg, uint16_t consumer_major,
                                  uint16_t consumer_minor, fixpp_engine_t** out_engine) {
    if (out_engine == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    *out_engine = nullptr;
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    // Pre-engine MAJOR gate (NOT routed through translate_for_consumer — there is
    // no engine yet to carry consumer_minor).
    if (consumer_major != FIXPP_C_ABI_VERSION_MAJOR) {
        return FIXPP_ERR_VERSION_MISMATCH;
    }

    fixpp_engine* e = nullptr;
    try {
        e = new fixpp_engine{};
        e->consumer_minor = consumer_minor;
        e->worker_threads_ = cfg->worker_threads == 0 ? 1U : cfg->worker_threads;
        e->app_ = std::make_shared<fixpp_capi::detail::CapiApplication>();

        fixpp::core::EngineConfig eng_cfg;
        eng_cfg.executor = e->ioc_.get_executor();
        if (cfg->want_realtime_clock) {
            e->clock_ = std::make_shared<fixpp::core::system_clock_source>(e->ioc_.get_executor());
            eng_cfg.clock = e->clock_;  // null clock → Engine::start() rejects (clock_not_set)
        }
        eng_cfg.application = e->app_;  // the trampoline (NOT consumer-settable)

        // LAST fallible step. Workers are NOT launched here — Engine::start() is
        // called (in fixpp_engine_start) while nothing polls the io_context (the
        // canonical sequencing: start() only POSTS the role loops; they run once
        // a worker drives the io_context), then the workers launch. This sidesteps
        // any race between start()'s synchronous body and a freshly-spawned loop.
        e->engine_.emplace(e->ioc_.get_executor(), std::move(eng_cfg));
    } catch (...) {
        // engine_ is left disengaged if emplace threw (std::optional guarantee),
        // and no workers were launched, so freeing e here destructs nothing that
        // asserts. (Belt: reset the work-guard so a future run() would return.)
        if (e != nullptr) {
            e->work_guard_.reset();
            delete e;
        }
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }

    // Consume the builder (moved logically into the engine; invalidated).
    delete cfg;
    *out_engine = e;
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_engine_start(fixpp_engine_t* engine) {
    if (engine == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (!engine->engine_.has_value()) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    if (engine->engine_started_) {
        // Second call. Engine::start() is legal once (engine.hpp).
        return fixpp_capi::detail::translate_for_consumer(
            fixpp_capi::detail::translate(fixpp::core::error::session_already_open),
            engine->consumer_minor);
    }

    // Run start() synchronously on THIS (consumer) thread while no worker polls
    // the io_context — start() only co_spawns the role loops; nothing executes
    // until a worker drives the loop. This matches the proven C++ test sequencing
    // (start() before ioc.run()) and avoids start()-vs-loop control-plane races.
    fixpp::core::expected_t<void> r = engine->engine_->start();
    if (!r.has_value()) {
        // Validated-and-failed (e.g. null clock): no loops spawned; leave
        // engine_started_ false so the error is reported as the config error,
        // not "already started".
        return fixpp_capi::detail::translate_for_consumer(
            fixpp_capi::detail::translate(r.error()), engine->consumer_minor);
    }
    engine->engine_started_ = true;

    // Launch the worker thread(s) to drive establishment. Each runs ioc_.run(),
    // kept alive by the work-guard until destroy() resets it.
    try {
        engine->workers_.reserve(engine->worker_threads_);
        for (std::uint32_t i = 0; i < engine->worker_threads_; ++i) {
            engine->workers_.emplace_back([engine] { engine->ioc_.run(); });
        }
    } catch (...) {
        // Thread creation failed. Any workers that DID launch still drive the
        // io_context; if none launched, destroy() drains stop() on the consumer
        // thread (the no-worker path). Either way the engine is recoverable via
        // destroy(); report a config error.
        if (engine->workers_.empty()) {
            return FIXPP_ERR_CAPI_CONFIG_INVALID;
        }
    }
    return FIXPP_ERR_OK;
}

// Dead-shell registry ([2i §4.2.1/§4.2.2]): destroyed engine shells are
// RETAINED here (never freed) so a second fixpp_engine_destroy(eng) call on
// the same pointer can safely read the DEAD tag without UAF.  Reachability
// via this root prevents ASan/LSan from reporting them as leaks.
//
// The registry itself is heap-allocated and deliberately NEVER deleted.  A
// static-storage std::vector<> has its destructor run at exit() time BEFORE
// LSan's atexit check fires; the resulting "all pointers freed by static dtor"
// state makes LSan classify the retained shells as leaks anyway.  Using a raw
// heap pointer that is never freed keeps the shells reachable during LSan's
// scan window and suppresses the false-positive.  The leaked registry object
// is small (one std::vector control block per process) and harmless.
//
// NOTE (L-050-z): each retained engine shell includes the full heap graph
// behind all its members (sessions_, app_, ioc_, clock_) — NOT just the
// sizeof(fixpp_engine) control bytes.  sessions_ and app_ CANNOT be freed
// because fixpp_session_t* consumer pointers live in sessions_, and
// session->slot borrows into app_->slots_.  This is an acceptable per-engine
// bounded cost for v1.0 (engines are process-lifetime per [2i §4.10]);
// reclaiming ioc_/clock_ is deferred per L-050-z in behaviors-and-limitations.
//
// fixpp_engine_destroy is SINGLE_THREAD ([2i §4.10]) so no lock is needed.
static std::vector<fixpp_engine_t*>* s_dead_shells = // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
    new std::vector<fixpp_engine_t*>();

void fixpp_engine_destroy(fixpp_engine_t* engine) {
    if (engine == nullptr) {
        return;  // NULL-safe
    }
    // Tombstone check ([2i §4.2.1]): a second fixpp_engine_destroy(eng) on the
    // same pointer is a no-op.  The shell is retained in s_dead_shells (not freed)
    // after the first destroy, so this tag read is always safe — no UAF possible.
    if (engine->tag_ == FIXPP_HANDLE_TAG_DEAD) {
        return;  // already destroyed — idempotent, no UAF
    }
    if (engine->engine_.has_value()) {
        // UNCONDITIONALLY drive Engine::stop() to completion (FR-003): ~Engine
        // asserts stopped(), which inits false and is only set by stop(), so even
        // a never-started engine must stop() or the dtor would std::abort. stop()
        // is idempotent from any state.
        auto stop_fut = asio::co_spawn(engine->ioc_, engine->engine_->stop(), asio::use_future);
        if (!engine->workers_.empty()) {
            // Workers drive the io_context → they execute stop().
            stop_fut.get();
        } else {
            // No worker running: drive the io_context on THIS thread until stop()
            // completes. Reset the work-guard first so run() returns once the
            // (idempotent, quick) stop() coroutine finishes with no other work.
            engine->work_guard_.reset();
            engine->ioc_.run();
            stop_fut.get();
        }
    }

    // Unblock any running workers and join them.
    engine->work_guard_.reset();  // idempotent
    for (auto& w : engine->workers_) {
        if (w.joinable()) {
            w.join();
        }
    }

    // Release the C++ Engine (calls ~Engine, which asserts stopped() — satisfied
    // above). After this, engine->engine_.has_value() == false, so any outstanding
    // session handle that reads check_session → !engine_->has_value() returns
    // FIXPP_ERR_INVALID_HANDLE safely (finding #2 fix).
    engine->engine_.reset();

    // Tombstone the handle then retain the shell in s_dead_shells.  The shell is
    // never freed: the dead-shell registry provides a reachable root (LSan
    // mark-sweep does not report it as a leak) while the tag guarantees the
    // second-destroy read-of-DEAD is safe from any thread that still holds the
    // original pointer.  See [2i §4.2.2] and the SHELL RETAIN note in
    // capi_internal.hpp.
    engine->tag_ = FIXPP_HANDLE_TAG_DEAD;
    s_dead_shells->push_back(engine);
}

}  // extern "C"
