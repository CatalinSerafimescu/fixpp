# Contract: `fixpp::session::Application` public interface

**Feature**: 019-app-callbacks | **Date**: 2026-06-03 | Status: design contract (not yet implemented)

The public callback interface a library user implements to observe/intercept FIX message flow. This is the **only** new public type the slice adds (plus the `EngineConfig::application` registration field and the `Engine::send(SessionId, …)` origination entry point). Header placement proposed: `include/fixpp/session/application.hpp` (cross-check `.specify/architecture.md` §4.4 layering at /implement — `fixpp::session::Application` is reserved there in the `session/` module, so the placement is ALLOWED with no `check_layers.py` map change).

## Interface sketch (illustrative — exact spelling finalised in TDD)

```cpp
namespace fixpp::session {

// All callbacks run on the target session's serialized executor (FR-010),
// invoked directly at on-strand sites. Return-value contract — NEVER throw to
// signal a normal outcome (FR-015). A thrown exception ⇒ the engine terminal-
// closes the session (FR-011).
class Application {
public:
    virtual ~Application() = default;

    // ── Lifecycle (notifications; no reject) ──────────────────────────────
    virtual void onCreate(const SessionId& id) {}                 // FR-009
    virtual void onLogon (const SessionId& id) {}                 // FR-009
    virtual void onLogout(const SessionId& id) {}                 // FR-009

    // ── Inbound delivery (after engine FSM processing — FR-003/FR-004) ────
    // value = accept; error = reject ⇒ session Reject(35=3)         (FR-005)
    virtual fixpp::core::expected_t<void>
        fromAdmin(const wire::MessageView<wire::access_mode::Index>& msg,
                  const SessionId& id) { return {}; }
    // value = accept; error = reject ⇒ BusinessMessageReject(35=j)   (FR-005)
    virtual fixpp::core::expected_t<void>
        fromApp(const wire::MessageView<wire::access_mode::Index>& msg,
                const SessionId& id) { return {}; }

    // ── Outbound interception (before transmit) ───────────────────────────
    // inspect only; admin not vetoable                              (FR-008)
    virtual void
        toAdmin(const wire::MessageView<wire::access_mode::Index>& msg,
                const SessionId& id) {}
    // value = send; error==app_do_not_send = veto; other error aborts (FR-007)
    virtual fixpp::core::expected_t<void>
        toApp(const wire::MessageView<wire::access_mode::Index>& msg,
              const SessionId& id) { return {}; }
};

} // namespace fixpp::session
```

`EngineConfig` gains: `std::shared_ptr<Application> application{nullptr};`
`Engine` gains: `asio::awaitable<fixpp::core::expected_t<void>> send(const SessionId&, std::span<const std::byte> app_payload);` (any-thread — FR-006; the caller awaits it — the await carries the post-completion veto/store/write outcome and is the outbound backpressure, no silent-drop queue).

## Method contracts

| Method | Pre | Post / effect | Threading | Failure |
|--------|-----|---------------|-----------|---------|
| `onCreate` | session object constructed + `open()` initialized `exec_`, before first Logon | user may init per-session resources | session strand (post-`open()`) | throw ⇒ terminal close (FR-011) |
| `onLogon` | session reached `Active` | user may begin originating sends | session strand | throw ⇒ terminal close |
| `onLogout` | session leaving established | user may release per-session resources | session strand | throw ⇒ terminal close |
| `fromAdmin` | inbound admin msg accepted by FSM | accept ⇒ normal; error ⇒ `Reject(35=3)` emitted | session strand | throw ⇒ terminal close |
| `fromApp` | inbound app msg accepted by FSM | accept ⇒ delivered; error ⇒ `BusinessMessageReject(35=j)` | session strand | throw ⇒ terminal close |
| `toAdmin` | engine about to emit admin msg | inspect; msg still sent | session strand | throw ⇒ terminal close |
| `toApp` | user/engine about to emit app msg | send / veto(`app_do_not_send`) / abort(other error) | session strand | throw ⇒ terminal close |
| `Engine::send` | — (any thread) | posts to session strand → `toApp` → emit; **awaitable** result resumes after post completes (natural backpressure); registry lookup holds a strong/owning session keepalive that outlives the post (014 class) | any thread (FR-006); returns `asio::awaitable<expected_t<void>>` | unknown id ⇒ `session_invalid_argument` (119); not established ⇒ `session_invalid_state_for_send` (77, FR-013); veto ⇒ `app_do_not_send` (129) |

## Guarantees

- **G1** — No two callbacks for the same session run concurrently (FR-010 / [const §XI.4]).
- **G2** — No callback runs after its session is destroyed; the Engine drains `exec_` before dtor ([L-015-4] / FR-012).
- **G3** — `application == nullptr` ⇒ zero callbacks, zero behavioural change vs pre-019 (FR-002 / SC-006).
- **G4** — Reject/veto is signalled by return value only; the engine never requires a throw and never lets a user throw reach its internals (FR-005/FR-007/FR-011/FR-015).
- **G5** — Inbound callbacks never observe a session-FSM-invalid message (FR-003/FR-004 / INV-6).
- **G6** — Parse→`fromApp` is zero-alloc (`const MessageView&`, no copy) — [const §VIII.5]; no post-queue on the inbound path — [const §XV.15].

## Out of contract (slice 1)

- In-place outbound **modification** in `toAdmin`/`toApp` (inspect+veto only — research D1).
- Per-session `Application` override (single per-engine — Clarifications Q2).
- `RejectLogon`-style logon veto from `fromAdmin` (lifecycle/admin reject of Logon is a follow-up).
- Config-file parsing, store/log factories, C ABI (spec Out of Scope).

## Normative References

Per `[const §VI.5]`: `[const §VIII.5]` (zero-alloc, G6), `[const §XI.4]` (strand serialization, G1), `[const §XIV.2]` (interface cap — 7 methods / 0 pure-virtual), `[arch §4.4]` (`Application` placement in `session/`), `[L-015-4]` (drain/keepalive, G2), `[FIX-SL §4.5.4]` (`Reject(35=3)`), `[FIX50SP2] Infrastructure / Business Rejects` (catalogue row A-014; `BusinessMessageReject(35=j)`).
