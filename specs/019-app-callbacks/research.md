# Phase 0 Research: Application Callback Layer (019-app-callbacks)

**Feature**: 019-app-callbacks | **Date**: 2026-06-03 | **Spec**: [spec.md](./spec.md)

Resolves the open design axes the spec deferred to `/speckit-plan` (chiefly the message-representation type) and grounds every clarified decision (Clarifications 2026-06-03) in the actual `Engine`/`Session` seams. Reconnaissance source: CodeGraph sweep of `engine.hpp`/`session.hpp`/`error.hpp`/`parser.hpp` (file:line cited inline).

## Existing seams (ground truth)

| Seam | Signature / type | Location |
|------|------------------|----------|
| Engine ctor + registry | `Engine(asio::any_io_executor, EngineConfig)`; `register_session(SessionConfig) → expected_t<void>` | `engine.hpp:171,175,190` |
| Session creation (onCreate site) | lazy, inside `run_accept_loop` / `run_connect_loop` | `src/session/engine.cpp` |
| Inbound entry | `Session::on_inbound_frame(std::span<const std::byte>) → awaitable<expected_t<void>>` (raw bytes; runs **on `exec_`**) | `session.hpp:233` |
| FSM state | `Session::state() → fsm_state {NotConnected,LogonSent,LogonReceived,Active,Disconnected}` | `session.hpp:245`, `session_fsm.hpp:30` |
| Outbound | `Session::send(std::span<const std::byte> app_payload) → awaitable<expected_t<void>>`; `write_gate_` async_mutex + `transport_send_` | `session.hpp:240,593,657` |
| Close (onLogout site) | `Session::close(close_mode::{graceful,terminal}) → awaitable<expected_t<void>>` | `session.hpp:116` |
| Callback seam | `dispatch_app_callback(F&&)` posts onto `exec_`; `executor() → const session_executor&` | `session.hpp:319,161` |
| Parsed message | `wire::MessageView<access_mode::Index>` (dict-backed `get<Tag>()`) | `parser.hpp` |
| Result type | `expected_t<T> = std::expected<T, error>` (C++23, throwless) | `error.hpp:737` |

## Decisions

### D1 — Message representation surfaced to callbacks: read-only `MessageView<Index>`

- **Decision**: Inbound callbacks (`fromApp`/`fromAdmin`) and outbound interception callbacks (`toApp`/`toAdmin`) receive a **read-only `const wire::MessageView<access_mode::Index>&`** (the existing dict-backed parsed view). The public origination entry point keeps the existing **`std::span<const std::byte> app_payload`** shape (the user builds the payload). **In-place field MODIFICATION** (stamping outbound messages) in `toApp`/`toAdmin` is **deferred** — slice 1 is inspect + veto only.
- **Rationale**: `MessageView<Index>` already exists and gives dict-aware field access without copying; passing it by `const&` keeps the parse→`fromApp` path **zero-alloc** per **[const §VIII.5]** (no per-message heap materialisation). A mutable-builder interception surface is a large, separable design (it would expose the `Writer`/builder mid-emit) and is not required for the G2 round-trip (the originator builds the full payload via `send`).
- **Alternatives rejected**: raw `std::span<const std::byte>` to callbacks (forces every user to re-parse — hostile, and re-parse alloc); an owning `Message` copy (heap allocation per message → violates [const §VIII.5] / [const §XV.1]).
- **Scope impact**: narrows spec FR-007/FR-008 "inspect/**modify**" to inspect + veto for slice 1. **Flag for Gate A** — recorded as a known limitation (B&L `L-019-*`), follow-up = mutable outbound interception.

### D2 — `Application` interface shape: abstract base, all-default virtuals, engine-registered

- **Decision**: `class fixpp::session::Application` — 7 `virtual` methods, **each with a default no-op / default-accept body (0 pure-virtual)**. Registered as `EngineConfig::application` of type `std::shared_ptr<Application>` (default `nullptr` ⇒ no callbacks, behaviour identical to pre-019 per FR-002). Session identity passed as `const SessionId&` (existing `engine.hpp:62`).
- **Return contract** (return-value, non-throwing — Clarifications Q1/FR-005/FR-015):
  - `onCreate/onLogon/onLogout(const SessionId&) → void` (notifications; no reject).
  - `fromAdmin(const MessageView&, const SessionId&) → expected_t<void>` — value = accept; `error` = reject ⇒ engine emits session-level `Reject(35=3)`.
  - `fromApp(const MessageView&, const SessionId&) → expected_t<void>` — value = accept; `error` = reject ⇒ engine emits `BusinessMessageReject(35=j)`.
  - `toAdmin(const MessageView&, const SessionId&) → void` — inspect only (admin not vetoable — spec Assumption).
  - `toApp(const MessageView&, const SessionId&) → expected_t<void>` — value = send; `error == error::app_do_not_send` = **veto** (DoNotSend); any other `error` aborts the send and is surfaced to the `send()` caller.
- **Rationale**: all-default virtuals = a user overrides only what they need (observer-pattern ergonomics) **and** the **pure-virtual count is 0**, satisfying **[const §XIV.2]** (≤5 pure-virtual) by the letter. `shared_ptr` registration gives a clean lifetime (Application outlives the Engine; see D3 drain). `expected_t` reuses the house throwless result type ([const §XV.9] no exceptions in the contract).
- **[const §XIV.2] justification (for Gate A)**: although the interface totals 7 methods, it is the **canonical, irreducible FIX-engine callback set** mirrored by QuickFIX-C++/J and Fix8: 3 lifecycle hooks (`onCreate` resource-init, `onLogon`/`onLogout` established-state edges), 2 inbound delivery hooks split by admin-vs-app (distinct reject semantics: session `Reject` vs `BusinessMessageReject`), and 2 outbound interception hooks split by admin-vs-app (distinct vetoability). None is collapsible without losing a distinct, standard FIX semantic. The all-default-virtual design keeps the *pure-virtual* surface at 0.
- **Alternatives rejected**: `std::function`-per-callback set (7 `std::function` members — larger, no clean "override subset", heap-y); template/`concept`-based Application (the `Engine` is a concrete runtime — a virtual base is the ABI-stable, reference-engine-matching choice); all-pure-virtual (forces 7 no-op overrides on every user, and *would* breach §2's pure-virtual cap).

### D3 — Invocation model: direct on-strand, not `asio::post`; only any-thread `send` posts

- **Decision**: `fromApp`/`fromAdmin`/`toAdmin`/`toApp` and `onCreate`/`onLogon`/`onLogout` are invoked **directly (synchronously) at the engine's existing on-`exec_` sites** (inbound processing in `on_inbound_frame`; the FSM transition points; the emit path), **not** re-posted via `asio::post`. The re-entrancy `dispatch_guard` logic (`session.hpp:336`) is applied at each direct call site. Only the **any-thread public `send`** (FR-006) does an `asio::post` onto the target session's `exec_` to reach the strand.
- **Rationale**:
  - **[const §VIII.5]** zero-alloc parse→`fromApp`: a direct call adds no queue node / closure allocation; an `asio::post` per inbound message would allocate a completion handler on the hot path.
  - **[const §XV.15]** no app-message drops: a `post` queue is an unbounded buffer that the banned-pattern rule forbids silently dropping from; invoking inline on the read-pump coroutine applies natural backpressure (the read-pump does not advance until the callback returns).
  - **[const §XI.4]** per-session-strand serialisation is preserved — these sites already run on `exec_`, so the strand invariant holds without a re-post; the debug `dispatch_guard` still asserts no concurrent entry.
  - Latency: avoids a strand re-schedule per message.
- **Refines spec assumption**: the spec's "delivered through the existing `dispatch_app_callback` seam (post onto the executor)" is refined to "delivered **on** the session executor, by direct invocation at on-strand sites; the post-based seam is used only by the any-thread `send`." The **[L-015-4]** drain contract is unchanged and still required (Engine `stop()` → `close(terminal)` + join-before-registry-clear drains the strand; the any-thread `send`'s detached write keeps a shared keepalive). **Flag for Gate A** (a documented divergence from the literal spec wording).
- **Alternatives rejected**: route everything through `dispatch_app_callback`'s `asio::post` (per-message handler alloc on hot path — violates §VIII.5; unbounded queue — §XV.15 hazard).

### D4 — Reject-response mapping

- **Decision**: `fromApp` reject ⇒ engine emits **`BusinessMessageReject(35=j)`** referencing the offending message (`RefMsgType(372)`, `RefSeqNum(45)`, a `BusinessRejectReason(380)`); `fromAdmin` reject ⇒ engine emits **session `Reject(35=3)`** (`RefSeqNum(45)`, `SessionRejectReason(373)`). For slice 1 the reason fields use a fixed/default code (refinement to per-error-code reasons deferred).
- **Open impl confirmation**: a `BusinessMessageReject(35=j)` builder may not yet exist (session emits `Reject(35=3)` today via 005/007). **Tasked at /tasks**: confirm/add a `BusinessMessageReject` builder. `Reject(35=3)` builder is established (005 FR-017 admin Reject path).
- **Rationale**: matches FIX-SL + reference-engine behaviour (QuickFIX maps `fromApp` `UnsupportedMessageType` → `BusinessMessageReject`; session-level errors → `Reject`).

### D5 — Throwing-callback disposition (FR-011)

- **Decision**: each direct callback invocation is wrapped in `try { ... } catch (...) { log; terminal close; }` at the dispatch boundary. The `dispatch_guard` RAII (`session.hpp:340`) already clears `in_dispatch_` on unwind, so a throw cannot leave the re-entrancy flag set. On catch ⇒ `close(close_mode::terminal)` + a recorded `SessionEvent`/error.
- **Rationale**: Clarifications Q5 — a throw is a fatal user-contract violation (reject/veto are return-value, D2). Terminal close is deterministic and never lets a user exception reach engine internals ([const §XV.9] spirit; no exception-driven control flow inside the engine).
- **New error slot (candidate)**: `error::app_callback_threw` (session-terminated-by-callback-exception) — minted at the next free slot (122+ used by 017; confirm free slot at /tasks).

### D6 — Public origination entry point

- **Decision**: `Engine::send(const SessionId&, std::span<const std::byte> app_payload) → expected_t<void>` — **any-thread-safe** (FR-006). It looks up the session in the registry, `asio::post`s onto that session's `exec_`, runs `toApp` (veto check), then the existing `Session::send` path (which stamps `34`/`52`, stores, writes under `write_gate_`). Off-strand callers reach the strand via the post; an on-strand caller (re-entrant send from inside a callback) is enqueued behind the current dispatch (no recursion/deadlock — FR-006 edge case).
- **Errors**: unknown `SessionId` ⇒ `error::session_invalid_argument` (slot 119, reuse); non-established session ⇒ `error::session_invalid_state_for_send` (slot 77, reuse — FR-013); `toApp` veto ⇒ `error::app_do_not_send`; `toApp` other error ⇒ that error.
- **Rationale**: registration is engine-level (D2), so origination is naturally `Engine`-keyed by `SessionId`. Reuses the proven `Session::send` durable-before-transmit path; adds only the `toApp` gate + the any-thread post.
- **Alternatives rejected**: expose a mutable `Session&` handle to the user (lifetime hazard — the Engine owns sessions and tears them down; a user-held `Session*` would race the registry clear, the exact [L-015-4]/014 UAF class).

### D7 — Error taxonomy additions

- New `error::` enumerators (mint at the next free slots ≥ 122, **confirm exact free slot at /tasks** since 017 minted 122–128): `app_do_not_send` (toApp veto sentinel), `app_callback_threw` (session terminated by a throwing callback). `BusinessMessageReject`/`Reject` reasons are wire fields (380/373), not `error` slots. Reuse existing 77 (`session_invalid_state_for_send`) and 119 (`session_invalid_argument`).

### D8 — Admin-vs-app classification

- **Decision**: reuse the engine's existing internal MsgType(35)-based admin/app classification (the `is_session_admin` logic referenced by 005/010 FR-017) to route inbound to `fromAdmin` vs `fromApp` and outbound to `toAdmin` vs `toApp`. No new classification surface.
- **Rationale**: single source of truth; avoids a second admin/app table drifting from the FSM's.

## Cross-cutting constraints confirmed

- **[const §XI.7] + §X.6 (Appendix A) — threading + (no C ABI) controls**: this is a threading-and-error-semantics feature ⇒ the four controls apply: `/clarify` ✅ (Session 2026-06-03), `/analyze` (pending, step 6), **Codex Gate A** (pending, mandatory), user `/plan` sign-off (pending). It is **not** a C-ABI change (C ABI explicitly out of scope), so [const §X] abidiff gate does not bind this slice.
- **[const §VII.3] TDD**: every callback site + reject mapping + throw disposition + any-thread send lands red-first.
- **[const §IX.1] coverage** ≥95/85 on touched `session/` files; reject/veto/throw error paths are *genuine* → must be tested (no silent uncovered error path).
- **[const §VII.6] interop**: this feature is the precondition that makes the v1.0 `Logon → NewOrderSingle → ExecutionReport → Logout` interop test (G2) implementable.
- **[const §VI] catalogue**: a new app-layer catalogue row (e.g. `APP-001` — Application callback interface) is added at the Polish/catalogue step.

## Open items carried to /tasks (impl confirmations, not blockers)

1. Confirm/add a `BusinessMessageReject(35=j)` builder (D4).
2. Confirm the next free `error::` slots ≥122 for `app_do_not_send` / `app_callback_threw` (D7).
3. Cross-check `Application` header placement against `decisions/architecture.md` layering (session/ vs a new application/ module) — propose `include/fixpp/session/application.hpp` ([const §XIV] + the §check_layers gate at /implement).
4. Confirm the exact on-strand FSM transition sites for `onLogon`/`onLogout` in `src/session/{session,engine}.cpp`.
