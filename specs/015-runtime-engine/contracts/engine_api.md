# Contract — Public Engine & SessionId API (015)

**Module**: `fixpp::session` (NOT a new module — R1). Header: `include/fixpp/session/engine.hpp`.
This is the **public-surface contract** Gate B reviews. Signatures are the design intent; exact spelling is locked at `/speckit-implement` against the shipped `Session`/`EngineConfig`/`SessionConfig` headers (no method name is invented here without a `file:line` anchor). Anchors re-derived first-hand at Gate A round 1.

---

## `fixpp::session::SessionId` (new value type — R6)

```cpp
namespace fixpp::session {

struct SessionId {
    std::string begin_string;     // SessionConfig::begin_string  (main)
    std::string sender_comp_id;   // SessionConfig::sender_comp_id (main)
    std::string target_comp_id;   // SessionConfig::target_comp_id (main)
    // No qualifier field (Gate A New-5: dropped — speculative until a real
    // SessionConfig qualifier lands).

    friend bool operator==(SessionId const&, SessionId const&) noexcept = default;

    // Own-role key (initiator + acceptor registry insertion).
    static SessionId from_config(SessionConfig const& cfg);

    // Acceptor resolution: reverse the inbound Logon's CompIDs (R4 / QFC/QFJ).
    //   result = { begin_string, sender = logon_target, target = logon_sender }
    static SessionId reversed_from_logon(std::string begin_string,
                                         std::string_view logon_sender_comp_id,
                                         std::string_view logon_target_comp_id);
};

}  // namespace fixpp::session
// + std::hash<SessionId> specialization (all three fields) for unordered_map keying.
```

**Contract**:
- Regular value type — value equality over all three fields; copyable, hashable.
- `from_config` and `reversed_from_logon` are the only two constructors used by the engine.
- No validation of empty fields here — `SessionConfig` open()-time validation (005/010) already rejects empties.

---

## `fixpp::session::Engine` (new concrete type — R1)

```cpp
namespace fixpp::session {

class Engine {
public:
    // Clarify-Q3: caller-supplied executor; the engine owns NO worker threads.
    Engine(asio::any_io_executor exec, fixpp::core::EngineConfig cfg);

    Engine(Engine const&) = delete;
    Engine& operator=(Engine const&) = delete;
    // move: deleted (holds spawned coroutine state + registry); pin via unique_ptr if needed.
    //
    // ~Engine(): STRICT precondition — asserts stopped(). NO synchronous
    // best-effort teardown (Gate A Codex-9): a synchronous destructor cannot
    // run the caller-driven executor to drain in-flight coroutines that hold
    // raw Session*, so freeing the sessions here is a UAF (the 014 Gate-B
    // burn). Destruction REQUIRES a prior `co_await stop()`.
    ~Engine();

    // FR-002: register a session (initiator or acceptor — role derived from cfg).
    // Records the SessionConfig only; does NOT construct a Session here (the
    // engine owns construction/open() inside the spawned loops — see start()).
    // Duplicate SessionId::from_config(cfg) → session_invalid_argument (R5 #2).
    // Must be called before start() in 015 (post-start registration is future work).
    // NO `Application&` parameter — there is no Application surface in 015
    // (FR-013; Gate A New-2). The real Session ctor is the public, synchronous
    // `Session(const EngineConfig&, const SessionConfig&)` (session.hpp:95) —
    // there is no make_session factory.
    [[nodiscard]] fixpp::core::expected_t<void> register_session(SessionConfig cfg);

    // FR-001/FR-003: non-blocking. co_spawns a connect loop per initiator and an
    // accept loop per acceptor on `exec`. Legal once. The caller drives `exec`
    // (e.g. io_context::run) — start() does NOT block or run the executor.
    // Each loop `co_await`s the awaitable Session::open() as its first step
    // (open() cannot run in this synchronous void start()); a session is thus
    // constructed-but-not-yet-open until its loop reaches open() (Gate A New-3).
    void start();

    // FR-011: idempotent, cancellation-safe teardown. Cancels every accept loop,
    // connect loop, read-pump, accept-scope domain, and in-flight handshake via
    // cancellation_type::total, closes transports, and JOINS all outstanding
    // session work BEFORE clearing the registry that owns the Session objects
    // (join-before-clear — Gate A New-4; prevents a session-strand read-pump
    // from dereferencing a freed Session*). A second stop() is a no-op. Returns
    // when teardown is complete (no leaked work, no UAF).
    [[nodiscard]] asio::awaitable<void> stop();

    // Registry addressing. Returns nullptr if `id` is not registered, OR is
    // registered but not yet established (e.g. an acceptor with no connected
    // peer, or a session whose loop has not yet reached open()) — null is not
    // an error (Gate A New-3).
    [[nodiscard]] Session* lookup(SessionId const& id) const;

    [[nodiscard]] bool stopped() const noexcept;
};

}  // namespace fixpp::session
```

**Lifecycle**: `constructed → started → stopping → stopped`. `start()` once; `stop()` idempotent from any state.

**Threading / cancellation contract**:
- All loops/pumps `co_spawn` on `exec`; each MUST `reset_cancellation_state(enable_total_cancellation())` so `stop()`'s total cancel actually propagates (`[const §XI.2]`; `[[feedback_asio_cospawn_total_cancellation_default]]`). A loop that omits this hangs `stop()` silently — the first thing to check on a stop-hang.
- Registry mutation/iteration is sequenced on an engine strand (E-5), NOT a `std::mutex` (`[const §XV.9]`). The engine strand protects the map; the join-before-clear rule (`stop()`) protects the pointee lifetime across strands (Gate A New-4).
- Per-session work stays on that session's strand (`Session::open()` binds it); the read-pump dispatches `on_inbound_frame` on the session strand (FR-004).

**Acceptor first-attach** (E-2 / research.md R7): on the acceptor path the accept loop runs the TLS handshake itself (`Listener::async_accept` returns a TCP-only transport — `listener.hpp:45-53`), harvests `handshake_result.peer_id`, and attaches via a **new acceptor-specific primitive** (design-named `attach_accepted_transport`) that sets `live_peer_id_` + rebinds outbound **without** an FSM transition. It does **NOT** use `install_reconnected_transport` (which re-enters `LogonSent`, initiator-only — Gate A Codex-2). The acceptor gate at `session.cpp:1048` then authorizes against `live_peer_id_` (live-identity-before-gate invariant, Gate A New-1).

**Outbound wiring** (E-1 / E-1a / Gate A Codex-4; Clarifications 2026-05-31): `transport_send_` is captured from `cfg_.transport_send` at `open()` as a **rebindable forwarding slot** (`session.hpp:530-534`) — initially a no-op under the engine's lazy-connect model, since the live transport does not exist at open() for **either** role. It is repointed at the live `Transport::async_write` when the transport becomes live: **acceptor** → during `attach_accepted_transport` (E-2); **initiator** → during `install_reconnected_transport` (E-1a). **015 amends `install_reconnected_transport` to ALSO rebind `transport_send_`** (symmetric with the acceptor attach); this changes the 014 behavior where that primitive left `transport_send_` untouched (valid only in 013/014's per-session-direct model, where the transport was bound at config time). This is why `lookup()` can return a registered acceptor (or not-yet-connected initiator) session before any connection (SC-004).

**Initiator connect path** (E-1a / FR-003 / SC-010 (7)/(8); Clarifications 2026-05-31): symmetric to the acceptor first-attach, the engine's `run_connect_loop` drives the initiator in **connect-then-Logon** order — grounded in QuickFIX-cpp (`setResponder` on connect → `next()`→`generateLogon`) and Fix8 (`connect()` → `send(generate_logon)`): the Logon is emitted strictly **after** the connection + outbound sink exist. The loop: `open()` (session/FSM/executor setup — but **does NOT emit the Logon at open** for an engine-driven initiator) → `co_await session.drive_reconnect()` (connect + handshake + authorize + `install_reconnected_transport`, which rebinds `transport_send_`) → **emit the initial Logon POST-connect** over the now-live `transport_send_` → read-pump on `session.live_transport()` until EOF → end. **Option A (2026-05-31)**: single connect+pump; multi-cycle reconnect-respin DEFERRED (`close(terminal)` is permanent; 014 has no tested multi-cycle reconnect). Two new **public** `Session` methods (SC-010 (7)/(8); both non-virtual, mirroring the public `attach_accepted_transport`):

```cpp
// Engine connect-loop driver — awaitable wrapper over the private
// reconnect_fsm_.drive_reconnect_attempt() (014's bounded reconnect loop).
[[nodiscard]] asio::awaitable<fixpp::core::expected_t<void>> drive_reconnect();

// Read-pump's live-transport source: the live reconnected_/accepted_transport_.
// (Exact spelling/const-ness locked at /speckit-implement.)
[[nodiscard]] fixpp::transport::Transport* live_transport() noexcept;
```

**`EngineConfig`**: the existing core value type (`include/fixpp/core/engine_config.hpp:106-148`) — supplies `clock`, `default_transport_factory`, dictionaries, default resources. It carries **NO `Application`**. The public `Session(const EngineConfig&, const SessionConfig&)` ctor (`session.hpp:95`) consumes it. The engine does NOT introduce a competing config type.

---

## Architecture.md obligation (R1/R6)
Add to architecture.md **§4.4 (`session` public types)** — NO §2.2/§2.3 layer-graph change:
- `fixpp::session::Engine` — public multi-session runtime engine (accept/connect loops, registry, lifecycle).
- `fixpp::session::SessionId` — registry key (FIX SessionID tuple).

Run `tools/check_layers.py` after `engine.hpp` lands (`[[feedback_gate_b_check_layers_post_fixer]]`).

## Pluggable-interface budget (`[const §XIV.2]`)
015 adds **no new pluggable interface** (dynamic-session-provider deferred — R2). `Engine` is a concrete type, not a pluggable interface; it does not count against any module cap. The new acceptor attach primitive (`attach_accepted_transport`) is a **non-virtual** method on the concrete `Session`, not a pluggable interface — it does not count either. If R2 is re-opened, the provider interface (1–2 methods) needs a one-paragraph Gate-A justification.
