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

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

// toApp runs ON the session strand BEFORE an application message is transmitted
// (originate-path tap only; resend retransmissions are NOT surfaced — L-019-4).
// Wrap the borrowed MessageView in a STACK fixpp_msg FRAMED read-only view (framing
// tags 8/9/34/49/52/56/10 ARE readable per "Framed toApp view" — contracts/
// toapp-callback.md). Map the C verdict to expected_t<void>:
//   FIXPP_TOAPP_SEND  (0) → {} (proceed)
//   FIXPP_TOAPP_VETO  (1) → unexpected(app_do_not_send)  (suppress, no seqnum consumed)
//   FIXPP_TOAPP_ERROR (2) → unexpected(app_callback_threw) (terminal-close)
//   out-of-range          → unexpected(app_callback_threw) (defined misuse path)
// A C callback cannot throw; if one escapes anyway it is an on-strand steady-state
// invariant violation → fatal-log + abort (same policy as fromApp). [D-8]
fixpp::core::expected_t<void> CapiApplication::toApp(
    const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
    const fixpp::session::SessionId& id) {
    SessionSlot* s = find_(id);
    if (s == nullptr || s->send_cb == nullptr) {
        return {};  // no callback registered → default send
    }
    fixpp_msg framed{};  // stack wrapper; FRAMED view (framing tags readable), D-8
    framed.view = &msg;
    fixpp_toapp_verdict verdict{};
    try {
        verdict = s->send_cb(&framed, s->send_userdata);
    } catch (...) {
        std::fputs(
            "fixpp C-ABI: send callback threw; aborting (on-strand steady-state "
            "invariant violation, FR-008)\n",
            stderr);
        std::abort();
    }
    // A misbehaving C callback may return an out-of-range value (a DEFINED misuse
    // path, FR-023). A typed enum LOAD of an out-of-range value is UB
    // (-fsanitize=enum), so read the raw bytes and switch on the integer instead;
    // an out-of-range value falls through to the ERROR arm below.
    int raw = 0;
    static_assert(sizeof(verdict) <= sizeof(raw), "verdict wider than int");
    std::memcpy(&raw, &verdict, sizeof(verdict));
    switch (raw) {
        case FIXPP_TOAPP_SEND:
            return {};
        case FIXPP_TOAPP_VETO:
            return std::unexpected(fixpp::core::error::app_do_not_send);
        case FIXPP_TOAPP_ERROR:
            return std::unexpected(fixpp::core::error::app_callback_threw);
        default:
            // Out-of-range verdict is a defined C-ABI-misuse path: treat as ERROR,
            // NOT silently coerced to send. [contracts/toapp-callback.md D-8]
            return std::unexpected(fixpp::core::error::app_callback_threw);
    }
}

// L-050-z witness seam: counts live EngineState instances.  Incremented in
// EngineState ctor, decremented in EngineState dtor (both in capi_internal.hpp
// via the extern reference).  live_state_count() is the test accessor.
std::atomic<long> g_engine_state_live_count{0};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

long live_state_count() noexcept {
    return g_engine_state_live_count.load(std::memory_order_relaxed);
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
        e = new fixpp_engine{};  // constructs state_ = make_unique<EngineState>()
        e->consumer_minor = consumer_minor;
        e->worker_threads_ = cfg->worker_threads == 0 ? 1U : cfg->worker_threads;
        e->app_ = std::make_shared<fixpp_capi::detail::CapiApplication>();

        fixpp::core::EngineConfig eng_cfg;
        eng_cfg.executor = e->state_->ioc_.get_executor();
        if (cfg->want_realtime_clock) {
            e->state_->clock_ = std::make_shared<fixpp::core::system_clock_source>(
                e->state_->ioc_.get_executor());
            eng_cfg.clock = e->state_->clock_;  // null clock → Engine::start() rejects
        }
        eng_cfg.application = e->app_;  // the trampoline (NOT consumer-settable)

        // LAST fallible step. Workers are NOT launched here — Engine::start() is
        // called (in fixpp_engine_start) while nothing polls the io_context (the
        // canonical sequencing: start() only POSTS the role loops; they run once
        // a worker drives the io_context), then the workers launch. This sidesteps
        // any race between start()'s synchronous body and a freshly-spawned loop.
        e->state_->engine_.emplace(e->state_->ioc_.get_executor(), std::move(eng_cfg));
    } catch (...) {
        // engine_ is left disengaged if emplace threw (std::optional guarantee),
        // and no workers were launched, so freeing e here destructs nothing that
        // asserts. (Belt: reset the work-guard so a future run() would return.)
        if (e != nullptr) {
            if (e->state_) {
                e->state_->work_guard_.reset();
            }
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
    if (engine->state_ == nullptr || !engine->state_->engine_.has_value()) {
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
    fixpp::core::expected_t<void> r = engine->state_->engine_->start();
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
        engine->state_->workers_.reserve(engine->worker_threads_);
        for (std::uint32_t i = 0; i < engine->worker_threads_; ++i) {
            engine->state_->workers_.emplace_back([st = engine->state_.get()] {
                st->ioc_.run();
            });
        }
    } catch (...) {
        // Thread creation failed. Any workers that DID launch still drive the
        // io_context; if none launched, destroy() drains stop() on the consumer
        // thread (the no-worker path). Either way the engine is recoverable via
        // destroy(); report a config error.
        if (engine->state_->workers_.empty()) {
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
// L-050-z RESOLVED: each retained dead shell holds ONLY the small identity
// graph (tag_, sessions_, app_, state_=nullptr).  The heavy EngineState
// (ioc_, work_guard_, clock_, engine_, workers_) is reclaimed by state_.reset()
// before the shell is pushed here.  sessions_ and app_ CANNOT be freed because
// fixpp_session_t* consumer pointers live in sessions_, and session->slot borrows
// into app_->slots_ — they must survive until the process exits.
//
// fixpp_engine_destroy is SINGLE_THREAD ([2i §4.10]) so no lock is needed.
static std::vector<fixpp_engine_t*>* s_dead_shells =  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
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

    // state_ is only null if fixpp_engine's ctor threw making state_ (then create
    // would have deleted e before returning). In normal usage state_ is non-null.
    if (engine->state_ && engine->state_->engine_.has_value()) {
        // UNCONDITIONALLY drive Engine::stop() to completion (FR-003): ~Engine
        // asserts stopped(), which inits false and is only set by stop(), so even
        // a never-started engine must stop() or the dtor would std::abort. stop()
        // is idempotent from any state.
        auto stop_fut = asio::co_spawn(engine->state_->ioc_,
                                       engine->state_->engine_->stop(),
                                       asio::use_future);
        if (!engine->state_->workers_.empty()) {
            // Workers drive the io_context → they execute stop().
            stop_fut.get();
        } else {
            // No worker running: drive the io_context on THIS thread until stop()
            // completes. Reset the work-guard first so run() returns once the
            // (idempotent, quick) stop() coroutine finishes with no other work.
            engine->state_->work_guard_.reset();
            engine->state_->ioc_.run();
            stop_fut.get();
        }
    }

    if (engine->state_) {
        // Unblock any running workers and join them BEFORE resetting state_.
        // Workers reference state_->ioc_; joining ensures no worker is alive
        // when state_ is freed.
        engine->state_->work_guard_.reset();  // idempotent; unblocks workers
        for (auto& w : engine->state_->workers_) {
            if (w.joinable()) {
                w.join();
            }
        }

        // Expire liveness tokens for ALL sessions BEFORE reclaiming the arena.
        // This covers the "engine destroyed without prior fixpp_session_close" path:
        // state_.reset() destroys EngineState::engine_ → all Session objects and
        // their session_arena_ are freed; any outbound fixpp_msg held by the
        // consumer must see token.lock()==nullptr before any arena dereference.
        // fixpp_session_close resets its own session's token too (path (a)); this
        // loop covers the discriminating path (b) where close was NOT called.
        // Invariant: token expiry happens-before arena teardown on every path (E-9).
        // [data-model E-9 / feedback_cabi_handle_destroy_needs_tombstone]
        for (auto& s : engine->sessions_) {
            if (s) {
                s->liveness_.reset();
            }
        }

        // Reclaim the heavy EngineState (ioc_, work_guard_, clock_, engine_,
        // workers_).  Destruction order inside EngineState (reverse declaration):
        //   engine_ first  — stopped(); borrows ioc_ executor (ioc_ still alive)
        //   work_guard_ second — references ioc_ (ioc_ still alive)
        //   ioc_ third
        //   clock_ last    — parked-sleep deregister hook runs at ioc_ teardown;
        //                    clock_ must outlive ioc_ (system_clock_source F2 belt #2)
        // workers_ is empty (all joined above) → harmless at dtor time.
        engine->state_.reset();
        // After state_.reset(): engine->state_ == nullptr.
        // check_session / fixpp_session_open see state_==nullptr before any
        // state_->engine_ dereference → FIXPP_ERR_INVALID_HANDLE (safe, no UAF).
    }

    // Tombstone the handle then retain the SMALL shell in s_dead_shells.
    // The shell now holds only: tag_(DEAD) + sessions_ + app_ + state_(null).
    // The heavy EngineState graph has been reclaimed above.
    engine->tag_ = FIXPP_HANDLE_TAG_DEAD;
    s_dead_shells->push_back(engine);
}

}  // extern "C"
