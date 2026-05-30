# Contract — Public Engine & SessionId API (015)

**Module**: `fixpp::session` (NOT a new module — R1). Header: `include/fixpp/session/engine.hpp`.
This is the **public-surface contract** Gate B reviews. Signatures are the design intent; exact spelling is locked at `/speckit-implement` against the shipped `Session`/`EngineConfig`/`SessionConfig` headers (no method name is invented here without a `file:line` anchor).

---

## `fixpp::session::SessionId` (new value type — R6)

```cpp
namespace fixpp::session {

struct SessionId {
    std::string begin_string;     // SessionConfig::begin_string  (session_config.hpp:153)
    std::string sender_comp_id;   // SessionConfig::sender_comp_id (:151)
    std::string target_comp_id;   // SessionConfig::target_comp_id (:152)
    std::string qualifier;        // reserved; always empty in 015 (no SessionConfig field yet)

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
// + std::hash<SessionId> specialization (all four fields) for unordered_map keying.
```

**Contract**:
- Value equality over all four fields; `qualifier` empty in 015 ⇒ no behavioural effect.
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
    ~Engine();   // asserts stopped(), or drives a synchronous best-effort teardown

    // FR-002: register a session (initiator or acceptor — role derived from cfg).
    // Duplicate SessionId::from_config(cfg) → error (R5 #2: reuse session_invalid_argument).
    // Must be called before start() in 015 (post-start registration is future work).
    [[nodiscard]] fixpp::core::expected_t<void> register_session(SessionConfig cfg);

    // FR-001/FR-003: non-blocking. co_spawns a connect loop per initiator and an
    // accept loop per acceptor on `exec`. Legal once. The caller drives `exec`
    // (e.g. io_context::run) — start() does NOT block or run the executor.
    void start();

    // FR-011: idempotent, cancellation-safe teardown. Cancels every accept loop,
    // connect loop, read-pump, and in-flight handshake via cancellation_type::total,
    // closes transports, and joins outstanding session work. A second stop() is a
    // no-op. Returns when teardown is complete (no leaked work, no UAF).
    [[nodiscard]] asio::awaitable<void> stop();

    // Registry addressing. Returns nullptr if no session for `id`.
    [[nodiscard]] Session* lookup(SessionId const& id) const;

    [[nodiscard]] bool stopped() const noexcept;
};

}  // namespace fixpp::session
```

**Lifecycle**: `constructed → started → stopping → stopped`. `start()` once; `stop()` idempotent from any state.

**Threading / cancellation contract**:
- All loops/pumps `co_spawn` on `exec`; each MUST `reset_cancellation_state(enable_total_cancellation())` so `stop()`'s total cancel actually propagates (`[const §XI.2]`; `[[feedback_asio_cospawn_total_cancellation_default]]`). A loop that omits this hangs `stop()` silently — the first thing to check on a stop-hang.
- Registry mutation/iteration is sequenced on an engine strand (E-5), NOT a `std::mutex` (`[const §XV.9]`).
- Per-session work stays on that session's strand (`Session::open()` binds it); the read-pump dispatches `on_inbound_frame` on the session strand (FR-004).

**Outbound wiring** (E-1): before constructing a `Session`, the engine sets `SessionConfig::transport_send` (the `std::function<void(span<const std::byte>)>` captured into `transport_send_` at open(), `session.hpp:530-534`) to a closure writing to the live `Transport::async_write`.

**`EngineConfig`**: the existing core value type (`include/fixpp/core/engine_config.hpp:106`) — supplies `clock`, `default_transport_factory`, dictionaries; the `Session(EngineConfig const&, SessionConfig const&)` ctor (`session.hpp:95`) already consumes it. The engine does NOT introduce a competing config type.

---

## Architecture.md obligation (R1/R6)
Add to architecture.md **§4.4 (`session` public types)** — NO §2.2/§2.3 layer-graph change:
- `fixpp::session::Engine` — public multi-session runtime engine (accept/connect loops, registry, lifecycle).
- `fixpp::session::SessionId` — registry key (FIX SessionID tuple).

Run `tools/check_layers.py` after `engine.hpp` lands (`[[feedback_gate_b_check_layers_post_fixer]]`).

## Pluggable-interface budget (`[const §XIV.2]`)
015 adds **no new pluggable interface** (dynamic-session-provider deferred — R2). `Engine` is a concrete type, not a pluggable interface; it does not count against any module cap. If R2 is re-opened, the provider interface (1–2 methods) needs a one-paragraph Gate-A justification.
