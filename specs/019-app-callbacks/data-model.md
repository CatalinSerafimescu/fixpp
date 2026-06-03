# Phase 1 Data Model: Application Callback Layer (019-app-callbacks)

**Feature**: 019-app-callbacks | **Date**: 2026-06-03

This feature is behavioural (a callback/observer surface), not data-storage; the "entities" are the public interface type, the values passed across it, and the registration/config touchpoints. Types are grounded in the existing codebase (see [research.md](./research.md)).

## Entities

### `Application` (new — the public callback interface)

The user-implemented observer/interceptor. Abstract base; **all methods are `virtual` with default no-op / default-accept bodies (0 pure-virtual** — [const §XIV.2]). A user subclasses and overrides only the callbacks they need.

| Callback | Signature (conceptual) | Returns | Default | Semantics |
|----------|------------------------|---------|---------|-----------|
| `onCreate` | `(const SessionId&)` | `void` | no-op | Session object created (before logon). Per FR-009. |
| `onLogon` | `(const SessionId&)` | `void` | no-op | Session reached established (`Active`). FR-009. |
| `onLogout` | `(const SessionId&)` | `void` | no-op | Session left established (graceful or terminal). FR-009. |
| `fromAdmin` | `(const MessageView&, const SessionId&)` | `expected_t<void>` | accept | Inbound **admin** msg, **after** FSM processing (FR-004). `error` ⇒ session `Reject(35=3)` (FR-005). |
| `fromApp` | `(const MessageView&, const SessionId&)` | `expected_t<void>` | accept | Inbound **app** msg, **after** FSM processing (FR-003). `error` ⇒ `BusinessMessageReject(35=j)` (FR-005). |
| `toAdmin` | `(const MessageView&, const SessionId&)` | `void` | no-op | Outbound **admin** msg before send; inspect only (not vetoable — FR-008). |
| `toApp` | `(const MessageView&, const SessionId&)` | `expected_t<void>` | send | Outbound **app** msg before send; `error == app_do_not_send` ⇒ veto (FR-007); other `error` aborts send. |

- **Lifetime**: held by the `Engine` via `std::shared_ptr<Application>`; MUST outlive every session the engine drives. The engine drains all in-flight callback work before destroying a session ([L-015-4], FR-012).
- **Invocation domain**: every callback runs on the target session's serialized executor (`exec_`), invoked **directly** at on-strand sites (research D3). No two callbacks for one session run concurrently (FR-010).
- **Throw contract**: a callback that throws ⇒ engine catches at the boundary, logs, terminal-closes the session (FR-011); never propagates inward.

### `SessionId` (existing — `engine.hpp:62`)

`{ begin_string, sender_comp_id, target_comp_id }`. Passed by `const&` to every callback so a multi-session user routes without ambiguity. Registry key.

### `MessageView<access_mode::Index>` (existing — `parser.hpp`)

Read-only, dict-backed parsed FIX message (`get<Tag>() → expected_t<field_view>`). The surface passed to the four message callbacks. Non-owning; valid only for the duration of the synchronous callback (no escape — the bytes are the read-pump's frame buffer).

### `EngineConfig::application` (new field on existing `EngineConfig`)

`std::shared_ptr<Application> application{nullptr}`. `nullptr` ⇒ no callbacks invoked, behaviour identical to the pre-019 engine (FR-002). Single per-engine registration (Clarifications Q2).

### Reject/veto value mapping (no new storage; wire + error)

| User signal | Engine action | Wire / error |
|-------------|---------------|--------------|
| `fromApp` returns `error` | emit business reject | `BusinessMessageReject(35=j)`: `RefMsgType(372)`, `RefSeqNum(45)`, `BusinessRejectReason(380)` |
| `fromAdmin` returns `error` | emit session reject | `Reject(35=3)`: `RefSeqNum(45)`, `SessionRejectReason(373)` |
| `toApp` returns `app_do_not_send` | drop the outbound (no transmit) | `send()` returns `error::app_do_not_send` to caller |
| `toApp` returns other `error` | abort the send | `send()` returns that `error` |
| any callback throws | terminal-close session | `error::app_callback_threw` recorded |

### New `error::` enumerators (mint at next free slot ≥122 — confirm at /tasks)

- `app_do_not_send` — `toApp` veto sentinel (FR-007).
- `app_callback_threw` — session terminated by a throwing callback (FR-011).

(Reused: `session_invalid_state_for_send=77` for send-before-logon FR-013; `session_invalid_argument=119` for unknown `SessionId`.)

## State / lifecycle (callback firing order, per session)

```
register_session(cfg)
        │
   [accept/connect loop constructs Session]
        │
   onCreate(id)                         ← FR-009 (before logon)
        │
   … Logon handshake (FSM → Active) …
        │
   onLogon(id)                          ← FR-009 (established)
        │
   ┌───────────────── established (Active) ─────────────────┐
   │ inbound admin  → [FSM processes] → fromAdmin(view,id)  │  FR-004 (after FSM)
   │ inbound app    → [FSM processes] → fromApp(view,id)    │  FR-003 (after FSM)
   │ Engine::send(id,payload) → toApp(view,id) → [emit]     │  FR-006/FR-007
   │ engine admin emit         → toAdmin(view,id) → [emit]  │  FR-008
   └────────────────────────────────────────────────────────┘
        │
   close(graceful|terminal)  OR  terminal disconnect  OR  callback-threw
        │
   onLogout(id)                         ← FR-009 (leaves established)
        │
   [Engine drains exec_ before Session dtor]   ← [L-015-4] / FR-012
```

## Invariants (testable)

- **INV-1**: with `application == nullptr`, the firing order above collapses to the pre-019 path — zero callback invocations, zero behavioural delta (FR-002/FR-014; SC-006).
- **INV-2**: for one session, callbacks are totally ordered (never concurrent) — the `exec_` strand + `dispatch_guard` (FR-010; SC-005).
- **INV-3**: no callback executes after its session is destroyed — Engine drains `exec_` before dtor (FR-012; SC-005).
- **INV-4**: a `fromApp`/`fromAdmin` reject produces exactly the mapped peer reject; an accept produces none (FR-005; SC-003).
- **INV-5**: a `toApp` veto transmits nothing; a non-veto transmits exactly once (FR-007; SC-004).
- **INV-6**: inbound callbacks never see a message that failed session-FSM validation (FR-003/FR-004).
