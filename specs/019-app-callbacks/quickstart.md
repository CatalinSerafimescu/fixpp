# Quickstart: Application Callback Layer (019-app-callbacks)

**Feature**: 019-app-callbacks | **Date**: 2026-06-03

How a library user observes inbound business messages, originates outbound ones, and is notified of session lifecycle — the surface that unblocks G2 (`NewOrderSingle → ExecutionReport`). Illustrative; exact spelling settles in TDD.

## 1. Implement an `Application`

Override only the callbacks you need — all have default no-op/accept bodies.

```cpp
#include <fixpp/session/application.hpp>

class MyApp : public fixpp::session::Application {
public:
    void onLogon(const fixpp::session::SessionId& id) override {
        // session is Active — safe to originate sends to `id`
    }

    // Inbound business message (after the engine's session-FSM checks)
    fixpp::core::expected_t<void>
    fromApp(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
            const fixpp::session::SessionId& id) override {
        auto msg_type = msg.get<fixpp::tag::MsgType>();        // e.g. "D" NewOrderSingle
        // … business logic …
        return {};                                              // accept
        // return fixpp::core::error::/*reject reason*/;        // ⇒ BusinessMessageReject(35=j)
    }

    // Optional: veto an outbound app message
    fixpp::core::expected_t<void>
    toApp(const fixpp::wire::MessageView<fixpp::wire::access_mode::Index>& msg,
          const fixpp::session::SessionId& id) override {
        // return fixpp::core::error::app_do_not_send;          // ⇒ DoNotSend
        return {};                                              // send
    }
};
```

## 2. Register it with the engine (single per-engine)

```cpp
fixpp::core::EngineConfig cfg = /* … */;
cfg.application = std::make_shared<MyApp>();   // outlives the engine; nullptr ⇒ no callbacks

fixpp::session::Engine engine(io_exec, cfg);
engine.register_session(session_cfg);          // onCreate fires when the session is built
engine.start();
```

## 3. Originate an outbound application message (any thread)

```cpp
std::array<std::byte, N> payload = build_new_order_single(/* … */);  // app fields only
auto r = engine.send(session_id, payload);     // any thread; posts onto the session strand
if (!r) { /* r.error(): not-established, unknown id, or DoNotSend */ }
```

`send` runs `toApp` first (veto check), then the durable-before-transmit path (stamps `MsgSeqNum(34)`/`SendingTime(52)`, stores, writes). A re-entrant `send` from inside a callback is enqueued behind the current dispatch — no deadlock.

## 4. Verify (acceptance ↔ spec)

| Check | Spec |
|-------|------|
| `fromApp` fires once per inbound app msg, on the session strand, with correct id | SC-001, FR-003, FR-010 |
| Full `NewOrderSingle → ExecutionReport` round-trip drivable through `send` + `fromApp` | SC-002 (G2) |
| `fromApp`/`fromAdmin` reject ⇒ correct peer reject; accept ⇒ none | SC-003, FR-005 |
| `toApp` veto ⇒ 0 transmits; non-veto ⇒ 1 transmit | SC-004, FR-007 |
| No concurrent callbacks / no callback after session destroyed (ASan/UBSan/TSan) | SC-005, FR-010/FR-012 |
| `application == nullptr` ⇒ existing suites unchanged | SC-006, FR-002/FR-014 |
| Callback throws ⇒ session terminal-closes, no engine corruption | FR-011 |
