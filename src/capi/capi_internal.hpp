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
#include <memory_resource>
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
    // E-6: send (toApp) callback — registered by fixpp_session_register_send_callback
    // (US6/T018).  Absent (nullptr) → CapiApplication::toApp returns {} (send).
    // Written pre-start (single-threaded); read on the session strand (fromApp path).
    fixpp_send_cb send_cb = nullptr;
    void* send_userdata = nullptr;
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

// Handle-liveness tag constants ([2i §4.2.2]). Stored in each handle struct at a
// known offset so fixpp_engine_destroy (and future entry-point guards) can detect
// an already-destroyed handle without dereferencing its (freed) internals. The
// dead tag is written AT THE END of destroy before the shell is appended to the
// retained-shell registry (see SHELL RETAIN note below). [const §XVI.3]
// PLACED HERE: must precede all handle structs that use these constants as
// default member initialisers (fixpp_msg::tag_ and fixpp_engine::tag_).
static constexpr std::uint32_t FIXPP_HANDLE_TAG_ENGINE = 0xF1ECE001u;
static constexpr std::uint32_t FIXPP_HANDLE_TAG_MSG    = 0xF1EC1E55u;  // live outbound msg
static constexpr std::uint32_t FIXPP_HANDLE_TAG_DEAD   = 0xDEADD1EDu;

// SessionLiveness — the control-block type for the per-session validity token.
//
// The "content" is intentionally empty: the token's sole purpose is to exist
// (strong-ref alive → session arena live; expired → arena may be freed).  The
// per-session fixpp_session shell holds the strong shared_ptr<SessionLiveness>;
// outbound fixpp_msg handles hold a weak_ptr<SessionLiveness> aimed at the same
// block.  The strong ref is reset on every arena-teardown path BEFORE the arena
// is freed, so a weak.lock() == nullptr is a safe "arena gone" signal.
//
// Expiry paths (E-9 §"MUST cover EVERY arena-reclamation path"):
//   (a) fixpp_session_close  — resets liveness_ alongside valid=false.
//   (b) fixpp_engine_destroy — loops sessions_ and resets each liveness_ BEFORE
//       state_.reset() reclaims EngineState::engine_ (→ Session::session_arena_).
// [data-model E-9 / feedback_cabi_handle_destroy_needs_tombstone]
struct SessionLiveness {};

// Outbound accumulator (E-3): arena-resident data structure built by the consumer
// via set_*/group_begin/etc. (US2/T009).  Declared here so fixpp_msg can hold a
// pointer to it without a forward-decl forward-decl gap; fully defined below.
struct OutboundAccumulator;

// Message flavour tag (E-1): distinguishes the two live fixpp_msg shapes.
enum class FixppMsgFlavour : std::uint8_t {
    inbound  = 0,  // stack flyweight; view is valid; accumulator == nullptr
    outbound = 1,  // heap shell; accumulator points into the session arena
};

// fixpp_msg — dual-flavour message handle (E-1 / data-model §E-1).
//
// INBOUND flavour (legacy Feature-B path, no change in observable behavior):
//   - Constructed on-stack inside CapiApplication::fromApp (engine.cpp); tag_ is
//     irrelevant for inbound because the handle never outlives the dispatch window.
//     Tag is set to FIXPP_HANDLE_TAG_MSG for uniformity so check_msg (US2) can
//     treat both flavours identically for the dead-tag arm.
//   - `view` points to the borrowed MessageView; `accumulator` is nullptr.
//   - `token` is default-constructed (expired) — check-before-deref (US2) returns
//     FIXPP_ERR_INVALID_HANDLE on any set_*/commit call, which is correct for
//     inbound: E-1 says inbound set_* → FIXPP_ERR_INVALID_HANDLE.
//
// OUTBOUND flavour (new in Feature C):
//   - Heap-allocated by fixpp_msg_create (US2/T009); tag_ = FIXPP_HANDLE_TAG_MSG.
//   - `accumulator` owns the in-arena accumulator (pointer into session arena);
//     `view` is nullptr.
//   - `token` is a weak_ptr<SessionLiveness> set at create-time from the owning
//     session's liveness_; expires when the session arena is torn down (E-9).
//     The fixpp_msg shell itself lives OUTSIDE the arena (operator new), so
//     reading tag_/token after arena teardown is safe — the mechanism closes the
//     050 arena-UAF class.
//
// DESTROYED state: tag_ flipped to FIXPP_HANDLE_TAG_DEAD by fixpp_msg_destroy.
// TOMBSTONED state: tag_ still FIXPP_HANDLE_TAG_MSG but token.lock() == nullptr.
//   → check_msg (US2) detects both and returns FIXPP_ERR_INVALID_HANDLE before
//   any arena dereference.
//
// [data-model E-1 / E-9 / feedback_cabi_handle_destroy_needs_tombstone]
struct fixpp_msg {
    std::uint32_t        tag_      = FIXPP_HANDLE_TAG_MSG;  // handle-liveness (E-1)
    FixppMsgFlavour      flavour   = FixppMsgFlavour::inbound;

    // Inbound arm: borrowed MessageView (valid only in receive-callback window).
    const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>* view = nullptr;

    // Outbound arm: pointer to the in-arena accumulator (non-owning; the session
    // arena owns the allocation).  nullptr for inbound handles.
    OutboundAccumulator* accumulator = nullptr;

    // Validity token (E-9 lazy tombstone).  Default-constructed = expired, which
    // is the correct sentinel for inbound (set_* → FIXPP_ERR_INVALID_HANDLE) and
    // for unset outbound handles.  Set from session->liveness_ at create_outbound
    // time (US2/T009).
    std::weak_ptr<SessionLiveness> token;
};

// OutboundAccumulator — the ordered, mutable, arena-resident structure (E-3).
//
// Lives in the session arena; fixpp_msg holds a NON-OWNING pointer to it (NOT
// inline in fixpp_msg — the token check-before-deref must read tag_/token from a
// heap shell that survives arena teardown; inlining the accumulator would make
// that read itself a UAF after arena teardown, defeating the lazy mechanism).
//
// Structural shape (E-3, INV-1..5):
//   msg_type  — the "35=" value (non-empty; INV-1).
//   entries   — ordered list of scalar and group entries.
//
// Each AccumulatorEntry is either:
//   scalar (tag != 0): tag + value_bytes (INV-2: non-empty after validate).
//   group  (tag == 0): a list of GroupInstance, each an ordered list of nested
//                      AccumulatorEntry (recursive for nested groups, FR-012 / E-4).
//
// Mutual recursion (AccumulatorEntry ↔ GroupInstance) is broken via
// std::unique_ptr: GroupInstance holds unique_ptr<AccumulatorEntry>[] elements so
// that only a POINTER to AccumulatorEntry (not its full layout) is needed at
// GroupInstance definition time.  AccumulatorEntry is then fully defined after.
// This is the standard recursive-struct technique; no arena PMR allocator is used
// for the unique_ptr nodes (the nodes themselves are arena-new'd in US2/T009 via
// the OutboundAccumulator::arena_ resource pointer passed down the call chain).
//
// INVARIANT: every allocation (msg_type, entry value_bytes, vectors, nodes) uses
// the session arena's memory_resource — no global-heap (INV-5 / [const §VIII.5]).
// US2/T009 must use `new(arena_->allocate(…)) AccumulatorEntry` patterns or
// pmr::vector with the resource propagated.
//
// All mutation (set_*, add_entry, commit, etc.) is US2/T009 — not present here.

// OutboundAccumulator node types — recursive group tree.
//
// The mutual recursion (AccumulatorEntry contains group instances, each instance
// contains AccumulatorEntry) is broken by defining GroupInstance to store pointers
// to AccumulatorEntry via a helper opaque list, with the destructor defined after
// AccumulatorEntry is complete.  See the GroupEntryList / GroupInstance comment.
//
// Layout at a glance (E-3):
//   OutboundAccumulator
//     .msg_type                   — the 35= value
//     .entries[]                  — ordered field/group list
//       AccumulatorEntry (scalar) — tag != 0, value_bytes non-empty
//       AccumulatorEntry (group)  — tag == 0, instances[]{…AccumulatorEntry…}
//
// All mutation (set_*, add_entry, commit) is US2/T009 — only shape here.
// All arena allocations use OutboundAccumulator::arena_.

// ── Forward declarations for the mutual-recursion cycle ───────────────────
struct AccumulatorEntry;

// GroupInstance: one repeating-group instance; holds an ordered list of
// AccumulatorEntry via pmr::vector<AccumulatorEntry>.  Because AccumulatorEntry
// is not yet complete here, vector operations on the fields member are deferred
// to .cpp (US2/T009), which sees the full definition.  The struct itself is
// defined as having a default-constructible vector (no element operations needed
// here) — this is legal: the vector data-member declaration is fine with an
// incomplete element type; it's the constructor/destructor/push_back that need
// completeness, and those are all deferred (user-defined dtors declared = delete
// here, defined in message_write.cpp after AccumulatorEntry is complete).
struct GroupInstance {
    std::pmr::vector<AccumulatorEntry> fields;  // element type complete in message_write.cpp

    // Constructor body deferred: defined in message_write.cpp (US2/T009) after
    // AccumulatorEntry is fully defined.  See the "GroupInstance_ctor" comment
    // there.  Only the declaration is emitted here.
    explicit GroupInstance(std::pmr::memory_resource* mr);
    ~GroupInstance();
    GroupInstance(GroupInstance&&) noexcept;
    GroupInstance& operator=(GroupInstance&&) noexcept;
    GroupInstance(const GroupInstance&) = delete;
    GroupInstance& operator=(const GroupInstance&) = delete;
};

// AccumulatorEntry: a single ordered entry — scalar or group.
struct AccumulatorEntry {
    std::uint16_t                   tag = 0;       // 0 == group; non-zero == scalar
    std::pmr::vector<std::byte>     value_bytes;   // scalar: serialised field bytes
    std::pmr::vector<GroupInstance> instances;     // group: repeating instances

    explicit AccumulatorEntry(std::pmr::memory_resource* mr)
        : value_bytes(mr), instances(mr) {}

    ~AccumulatorEntry() = default;
    AccumulatorEntry(AccumulatorEntry&&) = default;
    AccumulatorEntry& operator=(AccumulatorEntry&&) = default;
    AccumulatorEntry(const AccumulatorEntry&) = delete;
    AccumulatorEntry& operator=(const AccumulatorEntry&) = delete;
};

// OutboundAccumulator: the arena-resident root of the outbound message tree.
struct OutboundAccumulator {
    std::pmr::memory_resource*         arena_;    // session arena (non-owning)
    std::pmr::string                   msg_type;  // 35= value (INV-1: non-empty)
    std::pmr::vector<AccumulatorEntry> entries;   // ordered field/group list

    explicit OutboundAccumulator(std::pmr::memory_resource* mr)
        : arena_(mr), msg_type(mr), entries(mr) {}

    ~OutboundAccumulator() = default;
    OutboundAccumulator(OutboundAccumulator&&) = default;
    OutboundAccumulator& operator=(OutboundAccumulator&&) = default;
    OutboundAccumulator(const OutboundAccumulator&) = delete;
    OutboundAccumulator& operator=(const OutboundAccumulator&) = delete;
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

    // Liveness token (E-9): the STRONG owner of the per-session SessionLiveness
    // control block.  Constructed at session-shell creation time (so a strong ref
    // exists for the session's whole live span).  Reset on EVERY arena-teardown
    // path (fixpp_session_close + fixpp_engine_destroy before state_.reset()) so
    // that outbound fixpp_msg handles' weak_ptr<SessionLiveness>::lock() == nullptr
    // happens-before the session_arena_ is freed — closing the 050 UAF class.
    // The shell (fixpp_session) outlives the arena, so liveness_ itself is safe to
    // read/reset after session close/engine destroy. [data-model E-9]
    std::shared_ptr<SessionLiveness> liveness_ = std::make_shared<SessionLiveness>();
};

namespace fixpp_capi::detail {

// LIVE-STATE COUNTER — test seam for L-050-z witness.  Tracks the number of
// EngineState instances alive at any point.  Incremented in EngineState ctor,
// decremented in EngineState dtor.  Exposed only via live_state_count() (non-
// exported, detail namespace); NOT an exported C symbol.  See engine.cpp for
// the atomic and the accessor.
long live_state_count() noexcept;

}  // namespace fixpp_capi::detail

// Engine handle (E-1): owns the internal io_context + worker thread(s) + the C++
// Engine + the trampoline. The C++ Engine owns NO worker threads (engine.hpp:222
// — "the engine owns NO worker threads"); a C consumer has no executor to
// supply, so the C-ABI boundary owns one (research D-2).
//
// SPLIT INTO SHELL + EngineState (L-050-z fix):
//   The retained shell (fixpp_engine) holds only the IDENTITY members —
//   tag_, sessions_, app_, and a unique_ptr<EngineState> state_.  The heavy
//   members (ioc_, work_guard_, clock_, engine_, workers_) live in EngineState
//   and are reclaimed by state_.reset() inside fixpp_engine_destroy, immediately
//   after joining workers and destroying the C++ Engine.  Only the small shell
//   (+ sessions_ + app_) is retained in s_dead_shells for tombstone idempotency.
//
// EngineState DESTRUCTION ORDER (reverse of member declaration order):
//   engine_    — declared LAST → destroyed FIRST; MUST be stopped()
//               (~Engine asserts stopped(), engine.hpp:233); borrowed ioc_ must
//               still be alive (engine_ borrows the io_context executor).
//   work_guard_ — declared 3rd → destroyed 2nd; ioc_ still alive.
//   ioc_        — declared 2nd → destroyed 3rd.
//   clock_      — declared FIRST → destroyed LAST; the parked-sleep deregister
//               hook (system_clock_source F2 belt #2) reaches into the clock pimpl
//               at io_context teardown — clock_ must outlive ioc_.
//   workers_    — declared 4th (after work_guard_); all workers are joined inside
//               fixpp_engine_destroy BEFORE state_.reset(), so workers_ is empty
//               and harmless at State dtor time.
//
// SHELL RETAIN ([2i §4.2.1] double-destroy idempotency): fixpp_engine_destroy
// sets state_.reset() (reclaims heavy graph) then tag_ = FIXPP_HANDLE_TAG_DEAD,
// then pushes the (now-small) shell pointer into s_dead_shells (engine.cpp).
// The shell is never freed; ASan/LSan see it as reachable.  A second
// fixpp_engine_destroy(eng) reads the DEAD tag on the retained shell (state_ is
// null, tag_ is DEAD) and returns immediately — no UAF.
//
// GUARD ORDERING: every read of state_->engine_ / ioc_ / clock_ MUST short-
// circuit through the null-state / dead-tag guard FIRST.  check_session in
// session.cpp checks tag_==DEAD || state_==nullptr before dereferencing state_.
// This is what keeps PostEngineDestroySessionHandleIsInvalidHandle safe.
//
// NB (L-050-2): do NOT fork() a process holding a live fixpp_engine_t — fork()
// copies only the calling thread, leaving a dead worker.
// Live-count counter — defined in engine.cpp, declared here so EngineState
// ctor/dtor can reference it inline without a block-scope extern.
namespace fixpp_capi::detail {
extern std::atomic<long> g_engine_state_live_count;
}  // namespace fixpp_capi::detail

struct EngineState {
    // Declared in clock_→ioc_→work_guard_→workers_→engine_ order so
    // that reverse-declaration destruction runs:
    //   engine_ first, work_guard_ second, ioc_ third, clock_ last.
    // workers_ is between work_guard_ and engine_; all workers are
    // joined before EngineState is destroyed, so the order is inert.
    std::shared_ptr<fixpp::core::Clock> clock_;         // destroyed LAST
    asio::io_context ioc_;
    asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
    std::vector<std::thread> workers_;                  // joined before state_.reset()
    std::optional<fixpp::session::Engine> engine_;      // destroyed FIRST

    EngineState() : work_guard_(asio::make_work_guard(ioc_)) {
        fixpp_capi::detail::g_engine_state_live_count.fetch_add(1, std::memory_order_relaxed);
    }
    ~EngineState() {
        fixpp_capi::detail::g_engine_state_live_count.fetch_sub(1, std::memory_order_relaxed);
    }
    // Non-copyable, non-movable (io_context is neither).
    EngineState(const EngineState&) = delete;
    EngineState& operator=(const EngineState&) = delete;
};

struct fixpp_engine {
    std::uint32_t tag_ = FIXPP_HANDLE_TAG_ENGINE;                    // liveness tombstone
    std::shared_ptr<fixpp_capi::detail::CapiApplication> app_;        // retained (slot borrows)
    std::vector<std::unique_ptr<fixpp_session>> sessions_;            // retained (handle storage)
    std::unique_ptr<EngineState> state_;                              // reclaimed on destroy
    std::uint32_t worker_threads_ = 1;
    std::uint16_t consumer_minor = 0;
    bool engine_started_ = false;  // Engine::start() succeeded

    fixpp_engine() : state_(std::make_unique<EngineState>()) {}
};
