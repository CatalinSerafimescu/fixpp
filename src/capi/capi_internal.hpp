// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/capi_internal.hpp — engine-INTERNAL concrete definitions for the
// C-ABI Feature B opaque handles + the Application trampoline (CA-005/006/007).
//
// NOT a shipped header (lives under src/, never installed). Nothing here is
// exported: the concrete struct layouts are engine-internal (the public headers
// expose only incomplete forward typedefs — handles.h), and the trampoline /
// helpers live in namespace fixpp_capi::detail with internal linkage at the .so
// boundary (fixpp_capi.map exports only `fixpp_*`). [050 data-model E-1..E-6]
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

#include <asio/executor_work_guard.hpp>
#include <asio/io_context.hpp>

#include "fix/c_api/error.h"
#include "fix/c_api/handles.h"
#include "fix/c_api/session.h"  // fixpp_recv_cb, fixpp_session_role, fixpp_security_kind

#include "fixpp/core/clock.hpp"
#include "fixpp/core/engine_config.hpp"
#include "fixpp/dict/dictionary.hpp"
#include "fixpp/session/application.hpp"  // Application + wire::MessageView (via wire/parser.hpp)
#include "fixpp/session/engine.hpp"       // Engine, SessionId
#include "fixpp/session/session_config.hpp"

// ── Internal helpers + the engine-wide Application trampoline (E-5) ──────────

namespace fixpp_capi::detail {

// Engine-internal error coalescing (defined in src/capi/error.cpp — Feature A).
fixpp_error_t translate(fixpp::core::error e) noexcept;
fixpp_error_t translate_for_consumer(fixpp_error_t code, std::uint16_t consumer_minor) noexcept;

#ifdef FIXPP_TEST_HOOKS
// SC-006 steady-state-abort seam: arms fixpp_session_send to throw inside its try
// block so the catch(...)→abort path (FR-008/FR-019) is witnessed. Defined
// unconditionally in src/capi/session.cpp; only this declaration is gated, so a
// production caller cannot flip it. Test sets true, calls send (→ abort), restores.
void set_send_throw_hook(bool on) noexcept;
#endif  // FIXPP_TEST_HOOKS

// Per-session callback + established state, keyed by SessionId. Inserted ONLY
// before fixpp_engine_start (fixpp_session_open / fixpp_session_register_callback
// are construction-time, single-threaded by contract); the map is immutable
// after start, so concurrent reads — on the session strand (fromApp) and from
// any consumer thread (is_established) — need no lock (Article XV §9). The only
// mutable cell is `established`, written on the engine exec_ (onLogon/onLogout)
// and read from any thread; std::atomic makes that safe. Stored behind
// unique_ptr so addresses stay stable across rehash (a handle caches `&slot`).
struct SessionSlot {
    fixpp_recv_cb cb = nullptr;
    void* userdata = nullptr;
    std::atomic<bool> established{false};
};

// CapiApplication — the single per-engine Application receiver installed as
// EngineConfig::application. Routes onLogon/onLogout to the per-session
// `established` flag (so fixpp_session_is_established is a lock-free atomic read
// of the genuine logged-on state, not an off-strand read of Session::is_open())
// and fromApp to the registered C receive callback (CA-007 trampoline). [E-5]
class CapiApplication final : public fixpp::session::Application {
public:
    // Pre-start registration (single-threaded). Inserts an empty slot if absent.
    SessionSlot& slot_for(const fixpp::session::SessionId& id);

    void onLogon(const fixpp::session::SessionId& id) override;
    void onLogout(const fixpp::session::SessionId& id) override;
    fixpp::core::expected_t<void> fromApp(
        const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
        const fixpp::session::SessionId& id) override;

private:
    SessionSlot* find_(const fixpp::session::SessionId& id) noexcept;
    std::unordered_map<fixpp::session::SessionId, std::unique_ptr<SessionSlot>> slots_;
};

}  // namespace fixpp_capi::detail

// ── Concrete opaque-handle definitions (incomplete typedefs in handles.h) ────

// Engine-config builder (E-1): the reachable subset of EngineConfig; everything
// else takes engine defaults. The real EngineConfig is built at create-time
// (it needs the internal io_context's executor, which only exists then).
struct fixpp_engine_config {
    std::uint32_t worker_threads = 1;
    bool want_realtime_clock = false;
};

// Session-config builder (E-3): wraps a SessionConfig under construction.
struct fixpp_session_config {
    fixpp::session::SessionConfig cfg;
};

// Dictionary handle (CA: full create/destroy is Feature C). Feature B defines
// the concrete wrapper so the test-only dictionary seam (L-050-1) can inject a
// test-built Dictionary behind the C-ABI boundary.
struct fixpp_dict {
    std::shared_ptr<const fixpp::dict::Dictionary> dict;
};

// Inbound message handle (CA-007): a STACK wrapper over the borrowed MessageView
// handed to fromApp; valid only inside the receive-callback dispatch window
// ([2i §4.6], FR-012). Read accessors are Feature C; Feature B exposes only the
// opaque handle + its dispatch-window lifetime.
struct fixpp_msg {
    const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>* view = nullptr;
};

// Session handle (E-2): NON-owning observer keyed by SessionId. Stores the
// owning engine + the (stable) trampoline slot pointer; NEVER caches a
// shared_ptr<Session> (lookup() leases against Engine::lease_counter_, asserted
// zero by ~Engine in DEBUG — every op does a scoped lookup released before
// return). Storage is owned by fixpp_engine (the sessions_ vector keeps shells
// alive even after engine destroy — see fixpp_engine destruction note); `valid`
// flips false once close() returns. [2i §4.2.2]
struct fixpp_session {
    fixpp_engine* engine = nullptr;                  // borrowed (owning engine)
    fixpp::session::SessionId id;                    // registry key
    fixpp_capi::detail::SessionSlot* slot = nullptr;  // borrowed (lives in CapiApplication)
    std::atomic<bool> valid{true};                   // invalidated by close(); atomic for
                                                     // send-vs-close concurrent access (Q2)
};

// Handle-liveness tag constants ([2i §4.2.2]). Stored in each handle struct at a
// known offset so fixpp_engine_destroy (and future entry-point guards) can detect
// an already-destroyed handle without dereferencing its (freed) internals. The
// dead tag is written AT THE END of destroy before the shell is appended to the
// retained-shell registry (see SHELL RETAIN note below). [const §XVI.3]
static constexpr std::uint32_t FIXPP_HANDLE_TAG_ENGINE = 0xF1ECE001u;
static constexpr std::uint32_t FIXPP_HANDLE_TAG_DEAD   = 0xDEADD1EDu;

// Engine handle (E-1): owns the internal io_context + worker thread(s) + the C++
// Engine + the trampoline. The C++ Engine owns NO worker threads (engine.hpp:222
// — "the engine owns NO worker threads"); a C consumer has no executor to
// supply, so the C-ABI boundary owns one (research D-2).
//
// DESTRUCTION (reverse of declaration order) is load-bearing:
//   workers_   joined first — a joinable std::thread that destructs unjoined
//              calls std::terminate;
//   engine_    reset next — MUST be stopped() (~Engine asserts stopped(),
//              engine.hpp:233); after reset, engine_.has_value() == false;
//   work_guard_ / ioc_ left alive in the shell (see SHELL RETAIN note below);
//   app_ / clock_ LAST — a parked sleep's deregister hook reaches into the clock
//              pimpl at io_context teardown (system_clock_source F2 belt #2), so
//              the clock must outlive ioc_; both also back EngineConfig copies
//              held inside engine_, so they must outlive engine_ too.
//
// SHELL RETAIN ([2i §4.2.1] double-destroy idempotency): fixpp_engine_destroy sets
// tag_ = FIXPP_HANDLE_TAG_DEAD, then pushes the shell pointer into a
// process-global `s_dead_shells` vector (engine.cpp).  The shell is never freed;
// the registry root keeps the entire graph (shell + sessions_ entries) reachable,
// so ASan/LSan do not report a leak.  A second fixpp_engine_destroy(eng) reads
// the DEAD tag on the retained shell and returns immediately — no UAF.  The same
// reasoning protects session handle pointers (stored in sessions_ which stays
// alive in the shell): after engine destroy, check_session sees
// !engine_->engine_.has_value() and returns FIXPP_ERR_INVALID_HANDLE without any
// UAF. Engine shells are small (< 200 bytes) and process-lifetime; the retained
// allocation is acceptable. [2i §4.2.2] / findings Q1.
//
// NB (L-050-2): do NOT fork() a process holding a live fixpp_engine_t — fork()
// copies only the calling thread, leaving a dead worker (feedback_fork_
// inherited_asio_pool_deadlock). Create the engine in the child after fork.
//
// NB (L-050-z): the comment "Engine shells are small (< 200 bytes)" on the
// SHELL RETAIN note below refers only to sizeof(fixpp_engine) and is MISLEADING
// in isolation — the retained dead shell includes the full heap graph behind
// all shared_ptr/vector members (ioc_, clock_, app_ with its slots_ map, and
// every sessions_ entry).  This is an unbounded per-cycle leak under engine
// create/destroy churn; it is waived for v1.0 because engines are process-
// lifetime (`[2i §4.10]`).  See L-050-z in spec/behaviors-and-limitations.md.
struct fixpp_engine {
    std::uint32_t tag_ = FIXPP_HANDLE_TAG_ENGINE;                    // liveness tombstone
    std::shared_ptr<fixpp::core::Clock> clock_;                       // destroyed LAST
    std::shared_ptr<fixpp_capi::detail::CapiApplication> app_;
    std::vector<std::unique_ptr<fixpp_session>> sessions_;            // handle storage
    asio::io_context ioc_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::optional<fixpp::session::Engine> engine_;
    std::vector<std::thread> workers_;                               // joined in destroy
    std::uint32_t worker_threads_ = 1;
    std::uint16_t consumer_minor = 0;
    bool engine_started_ = false;  // Engine::start() succeeded

    fixpp_engine() : work_guard_(asio::make_work_guard(ioc_)) {}
};
