# Feature Specification: Application Callback Layer (Phase-5, slice 1)

**Feature Branch**: `019-app-callbacks`  
**Created**: 2026-06-03  
**Status**: Draft  
**Input**: User description: "Phase-5 application callback layer — the public Application interface that lets library users observe and intercept FIX message flow on a Session, unblocking application-layer (business-message) interop (G2: NewOrderSingle→ExecutionReport, the const §VII.6 v1.0-GA residual). This is the FIRST Phase-5 slice: the callback/observer interface plus its production wiring ONLY. Public surface — the canonical FIX-engine application callback set (onCreate/onLogon/onLogout, fromAdmin/fromApp, toAdmin/toApp) plus a public application-send entry point. Integration constraint: callbacks dispatch on the session's serialized executor via the dispatch_app_callback seam and MUST inherit the L-015-4 lifetime/drain contract. Scope boundary — config-file parsing, pluggable store/log factories, and the C ABI are OUT."

## Context

After `015-runtime-engine`, fixpp has a public `Engine` (Initiator/Acceptor) that drives the full transport→FSM→session lifecycle for multiple sessions, and a session send path. But **the engine has no public application-callback surface**: there is no way for a library user to observe inbound messages, intercept outbound messages, or be notified of session lifecycle transitions. The only message-delivery hook that exists is the **internal `Session::dispatch_app_callback` seam** (`include/fixpp/session/session.hpp:319`), which today has **zero production callers** — it exists solely as the future wiring point for this feature (catalogue L-015-3 marks the `Application` path as Phase-5).

This gap is the **direct blocker for G2 business-message interop** (`NewOrderSingle → ExecutionReport`, the `[const §VII.6]` v1.0-GA residual that `016`/`018` carried as an open item): you cannot assert an application-message round-trip against a reference engine when the library exposes no way to receive an inbound application message or originate an outbound one. This feature delivers exactly that surface — and nothing more.

This is the **first slice of Phase-5**. Phase-5 as a whole (the QuickFIX-style service wrapper) also includes config-file parsing, pluggable `MessageStore`/`Log` factories, and a C ABI; those are **explicitly out of scope here** (see Out of Scope) and are deferred to later Phase-5 slices. This slice is the `Application` callback interface plus its wiring into the existing `Session`/`Engine` production path.

Unlike `016`/`018` (tests-only), this feature **adds public production surface** (a new user-facing `Application` interface + an outbound application-send entry point + their engine wiring), so it carries a real Gate-A adjudication and a production-behavior Gate B.

## Clarifications

### Session 2026-06-03

- Q: How should `fromApp`/`fromAdmin` reject and `toApp` veto (DoNotSend) be signaled? → A: **fixpp-native return value** — callbacks return a typed result (error-code / `expected` / DoNotSend sentinel); rejection/veto is a normal return, never an exception (deliberate divergence from QuickFIX's exception API to fit fixpp's no-throw house style).
- Q: At what granularity is the `Application` registered? → A: **Single per-engine** — one `Application` registered with the `Engine`, invoked for all its sessions, session identity passed per call (QuickFIX-C++/J + Fix8 model). Per-session override stays out of this slice.
- Q: From which thread may the public application-send entry point be called? → A: **Any thread** — `send()` posts onto the session's serialized executor internally (preserving the strand invariant + `[L-015-4]` keepalive); a re-entrant send from inside an on-strand callback is handled without deadlock.
- Q: When are `fromApp`/`fromAdmin` invoked relative to the engine's own session-FSM processing? → A: **After engine session processing** — callbacks fire only after seqnum/FSM validation accepts the message; a message failing session-level checks never reaches the user (QuickFIX semantics).
- Q: What happens when a user callback unexpectedly throws (given return-value rejection)? → A: **Fail-safe terminal disconnect** — catch at the dispatch boundary, log, clear the re-entrancy guard, terminate the session (terminal close); a throw is a fatal user-contract violation, never propagated into engine internals.

### Session 2026-06-03 (Gate A round 1)

- Q: What is `Engine::send`'s result-delivery model, given it posts onto the session strand before running `toApp` + the async `Session::send` path? → A: **`Engine::send` returns `asio::awaitable<expected_t<void>>`** — the caller awaits it; the awaitable resumes only after the posted work completes, so the veto/store/write outcome is carried truthfully and the await gives natural backpressure (no unbounded silent-drop outbound queue). (Refines FR-006 / research D6.)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Receive inbound application & admin messages (Priority: P1)

A library user supplies an `Application` implementation to the engine. When an established session receives a message from the peer, the engine surfaces it to the user's callbacks: **application** messages (e.g. `NewOrderSingle`) to `fromApp`, and **session-administrative** messages (e.g. `Heartbeat`, `TestRequest`) to `fromAdmin`. Each callback runs on the session's serialized executor and carries the session/peer identity so a multi-session user can route correctly. A user callback may **reject** an inbound message; the engine then emits the appropriate session-level reject response (`fromApp` rejection → a business-level reject such as `BusinessMessageReject`; `fromAdmin` rejection → a session-level `Reject`) instead of treating the message as accepted.

**Why this priority**: Inbound delivery is the irreducible core of the callback layer and the half of G2 that lets a user observe a peer's business message at all. It is independently demonstrable (feed a frame, observe the callback) and is the foundation every other story builds on. A viable MVP on its own.

**Independent Test**: Register an `Application` on an engine-driven session, drive the session to `Active`, feed it an inbound application frame and an inbound admin frame, and assert `fromApp` / `fromAdmin` each fire exactly once on the session executor with the correct message and session identity; then have the callback reject one inbound message and assert the engine emits the corresponding reject response on the wire.

**Acceptance Scenarios**:

1. **Given** an established session with a registered `Application`, **When** the peer sends an application message, **Then** `fromApp` is invoked exactly once with that message and the session identity, on the session's serialized executor.
2. **Given** an established session with a registered `Application`, **When** the peer sends a session-administrative message, **Then** `fromAdmin` is invoked exactly once with that message and the session identity, on the session's serialized executor.
3. **Given** a registered `Application` whose `fromApp` rejects a message, **When** that application message arrives, **Then** the engine emits the appropriate business-level reject response to the peer and does not treat the message as accepted.
4. **Given** a registered `Application` whose `fromAdmin` rejects a message, **When** that admin message arrives, **Then** the engine emits a session-level `Reject` to the peer.

---

### User Story 2 - Originate and intercept outbound application messages (Priority: P2)

A library user can **originate** an outbound application message on an established session through a public send entry point (today the post-`015` send path is internal/app-payload-only with no public `Application`-driven originate API). Before any outbound message leaves the engine, the user's interception callbacks fire: `toApp` for application messages — where the user may **inspect** or **veto** the send (`DoNotSend` semantics) — and `toAdmin` for engine-originated administrative messages, where the user may **inspect** (but the message is still sent; admin messages are not vetoable). A vetoed application message is never transmitted. (In-place outbound **modification** — stamping fields mid-emit — is deferred to a later Phase-5 slice; slice 1 is inspect + veto for `toApp`, inspect-only for `toAdmin`.)

**Why this priority**: Outbound origination + interception is the second half of a business-message round-trip (e.g. an acceptor replying `ExecutionReport`), completing G2. It is independently testable (call send, inspect the produced wire bytes) but depends conceptually on the same wiring proven by US1, so it follows P1.

**Independent Test**: On an established session, call the public application-send entry point with an application message and assert it crosses the wire after `toApp` fires; separately, register a `toApp` that vetoes and assert the message is not transmitted; drive an engine-originated admin message and assert `toAdmin` fires and the admin message is still sent (the engine's own required header stamping is unaffected; `toAdmin` is inspect-only this slice).

**Acceptance Scenarios**:

1. **Given** an established session with a registered `Application`, **When** the user calls the application-send entry point with an application message, **Then** `toApp` is invoked before transmission and the message crosses the wire.
2. **Given** a registered `Application` whose `toApp` vetoes (`DoNotSend`) a message, **When** the user attempts to send that application message, **Then** the message is not transmitted and the session remains `Active`.
3. **Given** an established session, **When** the engine originates an administrative message, **Then** `toAdmin` is invoked before transmission and the message is still sent (admin messages are not vetoable).
4. **Given** a session that is not established, **When** the user attempts to originate an application message, **Then** the send is refused with a defined error and nothing is transmitted.

---

### User Story 3 - Be notified of session lifecycle transitions (Priority: P3)

A library user is notified when a session is created and when it becomes / ceases to be established: `onCreate` after the engine creates the session object and `Session::open()` initializes its executor but **before** first Logon processing/emission (so `onCreate` runs on the engine's `exec_` — the executor is valid only post-`open()`), `onLogon` when the session reaches `Active`, and `onLogout` when it leaves the established state (graceful or terminal). These notifications let the user gate origination (send only after `onLogon`) and release per-session resources (on `onLogout`).

**Why this priority**: Lifecycle notifications improve usability and correctness of the user's send timing but are not strictly required to demonstrate a single observed/originated message; they layer cleanly on top of US1/US2 wiring. Lowest urgency of the three.

**Independent Test**: Drive a session through create → logon → logout under the engine and assert `onCreate`, `onLogon`, `onLogout` each fire exactly once, in order, on the session executor, with the correct session identity.

**Acceptance Scenarios**:

1. **Given** a registered `Application`, **When** the engine creates a session and `open()` initializes its executor, **Then** `onCreate` fires once with the session identity on the session strand, before any Logon is processed or emitted.
2. **Given** a registered `Application`, **When** a session reaches the established (`Active`) state, **Then** `onLogon` fires once with the session identity.
3. **Given** an established session, **When** it leaves the established state (graceful logout or terminal disconnect), **Then** `onLogout` fires once with the session identity.

---

### Edge Cases

- **Callback throws (any site)**: a user `fromApp`/`fromAdmin`/`toApp`/`toAdmin`/`onCreate`/`onLogon`/`onLogout` that throws is a fatal user-contract violation (rejection/veto is return-value-based, not exception-based) → the engine catches at the dispatch boundary, logs, clears the re-entrancy guard, and **terminates the session (terminal close)**; the exception never reaches engine internals (FR-011).
- **Send before logon / after logout**: originating an application message on a non-established session is refused with a defined error (no partial/queued transmission unless an explicit store-and-forward behavior is specified).
- **Session torn down with callback work in flight**: the engine MUST drain all dispatched callback work (and any detached outbound write) before a session is destroyed — the `[L-015-4]` lifetime contract — so no callback ever runs against a freed session (no use-after-scope / data race under sanitizers).
- **Re-entrant send from within a callback**: because `Engine::send` is any-thread-safe and **posts** onto the session executor (FR-006), a user calling it from inside an on-strand inbound callback is enqueued behind the current dispatch rather than recursing — no deadlock, and the single-in-flight-callback serialization invariant is preserved. This guarantee is **conditional on the post-only model**: the posted send cannot run until the current synchronous callback unwinds the strand, so a callback issues the re-entrant send (and may `co_await` its awaitable from a coroutine context) but MUST NOT synchronously block the strand waiting on that send's completion.
- **No `Application` registered**: a session driven with no registered `Application` behaves exactly as the pre-019 engine (callbacks are simply not invoked; no behavioral regression).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The library MUST expose a public `Application` interface comprising the callbacks `onCreate`, `onLogon`, `onLogout`, `fromAdmin`, `fromApp`, `toAdmin`, `toApp`, each receiving the session/peer identity.
- **FR-002**: A user MUST be able to register a **single** `Application` with the **engine**, whose callbacks are invoked for **all** of that engine's sessions (the QuickFIX-C++/J + Fix8 model), with the session identity passed per call; a session with no registered `Application` MUST behave exactly as the pre-019 engine (no callback invocation, no behavioral change). Per-session `Application` override is out of scope for this slice.
- **FR-003**: For each inbound application message on an established session — **after** the engine's own session-FSM processing (seqnum/FSM validation) has accepted it — the engine MUST invoke `fromApp` exactly once with the message and session identity. A message that fails session-level checks MUST NOT reach `fromApp`.
- **FR-004**: For each inbound session-administrative message on an established session — **after** the engine's own session-FSM processing has acted on it — the engine MUST invoke `fromAdmin` exactly once with the message and session identity. A message that fails session-level checks MUST NOT reach `fromAdmin`.
- **FR-005**: Rejection MUST be signaled by the callback's **return value** (a typed result / error-code), never by a thrown exception. A `fromApp` rejection MUST cause the engine to emit the appropriate business-level reject response to the peer and not treat the message as accepted; a `fromAdmin` rejection MUST cause the engine to emit a session-level `Reject`.
- **FR-006**: The library MUST expose a public entry point for a user to originate an outbound **application** message on an established session. This entry point MUST be callable from **any thread**: it posts the work onto the session's serialized executor internally and returns an awaitable (`asio::awaitable<expected_t<void>>`) the caller awaits, so the veto/store/write outcome is carried truthfully and the await applies natural backpressure (no unbounded silent-drop outbound queue). The post MUST preserve the single-in-flight-callback strand invariant and the `[L-015-4]` keepalive; a re-entrant call from inside an on-strand callback MUST NOT deadlock (the entry point posts and does not synchronously block the strand on its own completion).
- **FR-007**: Before transmitting an outbound application message, the engine MUST invoke `toApp`, allowing the user to **inspect** the message or **veto** it (`DoNotSend`); the veto MUST be signaled by **return value** (not an exception), and a vetoed message MUST NOT be transmitted. (In-place outbound modification is deferred to a later Phase-5 slice — slice 1 is inspect + veto only.)
- **FR-008**: Before transmitting an engine-originated administrative message, the engine MUST invoke `toAdmin`, allowing **inspection**; admin messages MUST still be transmitted (not vetoable). (In-place outbound modification is deferred to a later Phase-5 slice — slice 1 is inspect-only.)
  - **Amended 2026-06-14 (036-admin-emit-toadmin-coverage):** the *coverage* of this contract was extended from partial to full — at 019/027/033 ten engine-originated emit sites still bypassed observation. 036 wires the entire `Reject(35=3)` family + the initiator Guard-3 Logon-ack `Logout` through `toAdmin` (inspect-only, unchanged semantics), and routes the engine-originated `BusinessMessageReject(35=j)` through `toApp` (it is an application message, not admin — with full veto parity, the rejected inbound message's durable sequence advance still persisting). No semantic change to this FR's inspect-only/not-vetoable rule for admin frames; only the site coverage and the `35=j`→`toApp` routing were added. See `specs/036-admin-emit-toadmin-coverage/` + behaviors-and-limitations.md B-036-1. (Append-only note — no rewrite of merged 019 history.)
- **FR-009**: The engine MUST invoke `onCreate` once after a session is created and its executor is initialized by `open()` but **before** first Logon processing/emission (so `onCreate` runs on the session strand), `onLogon` once when a session becomes established, and `onLogout` **exactly once** when a session leaves the established state — pinned to the single idempotent `Active → !Active` state-transition edge (fire-once-per-session guard) so the graceful-close, terminal-close, and callback-threw-teardown exit paths cannot double-fire or miss it.
- **FR-010**: All `Application` callbacks for a given session MUST execute on that session's serialized executor; the engine MUST NOT invoke two callbacks for the same session concurrently (the strand invariant the `dispatch_app_callback` seam asserts in debug builds).
- **FR-011**: Because rejection/veto is return-value-based (FR-005/FR-007), a thrown exception from a user callback is an **unexpected fatal user-contract violation**: at every callback site (inbound, outbound, lifecycle) the engine MUST catch it at the dispatch boundary, log it, clear the re-entrancy guard, and **terminate the session via terminal close** — never propagating the exception into engine internals and never corrupting session state.
- **FR-012**: The engine MUST drain all dispatched `Application` callback work — and any detached outbound write originated by a callback — before destroying a session, satisfying the `[L-015-4]` lifetime/drain contract (`stop()` → `close(terminal)` + join-before-registry-clear; detached writes hold a shared keepalive per the 014 fix). The any-thread `Engine::send` post MUST capture a strong/owning reference to the target session (e.g. `shared_ptr<Session>` or a registry keepalive) that outlives the posted work, and `stop()`'s join MUST drain posted-but-not-yet-run sends before the registry clear, so a `stop()`/registry-clear racing the post cannot dereference a freed session (the 014 detached-write UAF class). No callback may run against a destroyed session.
- **FR-013**: Originating an application message on a session that is not established MUST be refused with a defined error, transmitting nothing.
- **FR-014**: The feature MUST NOT regress any existing session/engine behavior when no `Application` is registered, and MUST NOT alter the wire behavior of the session-administrative paths shipped in 005/013/015 beyond surfacing them to `fromAdmin`/`toAdmin`.
- **FR-015**: The `Application` callback contract is **return-value / non-throwing**: the library MUST NOT require user callbacks to throw to signal any normal outcome (accept, reject, or veto), and MUST define every callback's outcome as an explicit return value.

### Key Entities *(include if feature involves data)*

- **Application**: the user-implemented callback interface (lifecycle + inbound + outbound hooks). Registered with the engine; not owned by any single session.
- **Session identity**: the per-session/peer key passed to every callback so a multi-session user can route (the existing CompID/peer-identity notion from 013/014/015).
- **Message (admin vs application)**: the FIX message surfaced to or originated through the callbacks; the engine classifies each as administrative or application to route it to the correct callback pair.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user-registered `Application` receives **100% of inbound application messages** on an established session via `fromApp`, exactly once each and in arrival order.
- **SC-002**: An application-message round-trip over **opaque app payloads** (`std::span<const std::byte>`) can be driven through the public surface as an **enabling witness** for G2 — one side originates via the send entry point, the other observes via `fromApp` — i.e. G2 (`NewOrderSingle → ExecutionReport`) becomes *implementable* on top of this feature. The full typed `NewOrderSingle → ExecutionReport` QuickFIX-interop cell is a downstream feature (catalogue `A-001`/`A-006`/`A-014` remain backlog); this slice delivers the opaque-payload surface that unblocks it.
- **SC-003**: A `fromApp`/`fromAdmin` rejection produces the correct peer-visible reject response in **100%** of rejection cases; an accepted message produces none.
- **SC-004**: A `toApp` veto (`DoNotSend`) results in the message being transmitted **0%** of the time, while a non-vetoed message is transmitted 100% of the time.
- **SC-005**: Across the full lifecycle, **no `Application` callback ever executes concurrently with another callback for the same session**, and **no callback ever executes against a destroyed session** — verified clean under ASan/UBSan/TSan.
- **SC-006**: With no `Application` registered, **zero** observable behavioral difference versus the pre-019 engine (existing session/engine test suites pass unchanged).

## Assumptions

- **Registration granularity** *(resolved — Clarifications 2026-06-03; FR-002)*: a single `Application` is registered with the engine and invoked for all of that engine's sessions (the QuickFIX/QuickFIX-J/Fix8 model), with the session identity passed as a parameter. Per-session `Application` override is **not** included in this slice.
- **Admin vetoability**: `toAdmin` is inspect-only and cannot veto (admin messages are always sent); only `toApp` supports `DoNotSend`. (Matches QuickFIX semantics. In-place modification is deferred to a later Phase-5 slice.)
- **Rejection/veto mechanism** *(resolved — Clarifications 2026-06-03; FR-005/FR-007/FR-011/FR-015)*: callbacks signal accept/reject/veto by **return value** (typed result / error-code / DoNotSend sentinel), never by a thrown exception; an unexpected throw is a fatal user-contract violation handled by terminal session close. (Deliberate divergence from QuickFIX's exception-based API to fit fixpp's no-throw house style.)
- **Message representation**: the concrete type surfaced to callbacks (and accepted by the send entry point) is a design decision for `/speckit-plan`; the spec fixes only that an admin-vs-application FIX message is surfaced/accepted with its session identity.
- **Wiring point** *(refined — research D3, plan §VIII.5/§XV.15)*: callbacks execute on the engine's `exec_` under single-thread executor confinement (015 E-5); the inbound (`fromAdmin`/`fromApp`), lifecycle (`onCreate`/`onLogon`/`onLogout`), and emit (`toAdmin`/`toApp`) callbacks are invoked **directly** at the existing on-executor sites (no per-message `asio::post`, preserving zero-alloc §VIII.5 and no-drop §XV.15); the post-based `Session::dispatch_app_callback` seam is used **only** by the any-thread public `Engine::send` to reach the executor. Serialization derives from single-thread confinement (INV-2, L-019-3), not from a per-session strand. A future slice may add true per-session-strand confinement under a multi-threaded executor. This feature adds the public `Application` surface and its engine wiring; it does not introduce a second executor or change the session threading model.
- **Reuse of shipped paths**: the session-admin FSM, liveness, and recovery behaviors (005/013/015) are reused as-is; this feature surfaces them to `fromAdmin`/`toAdmin` and adds the application-message origination/delivery path, rather than re-implementing session behavior.

## Out of Scope

The following are part of Phase-5 overall but **explicitly deferred to later Phase-5 slices** and MUST NOT be implemented here:

- **Config-file (`.cfg`) parsing** / session settings ingestion.
- **Pluggable `MessageStore` / `Log` factory surface** (the user-supplied store/log plug-in model).
- **C ABI** / foreign-language binding surface.
- **Per-session `Application` override** (see Assumptions) unless `/speckit-clarify` pulls it in.
- Any change to the session threading model, the transport layer, or the wire behavior of existing session-admin paths beyond surfacing them to callbacks.

## Normative References

Per `[const §VI.5]`, these are the exact anchors this spec relies on:

- **`[const §VIII.5]`** Allocator discipline — zero-alloc parse→`fromApp` hot path (FR-003; plan §VIII.5).
- **`[const §XI.4]`** Per-session-strand serialization, callbacks never on the I/O thread (FR-010; SC-005).
- **`[const §XIV.2]`** Interface surface cap (≤5 pure-virtual) — the 7-method `Application` is 0 pure-virtual (plan Complexity Tracking; FR-001).
- **`[const §XV.15]`** No app/session message drop — direct on-strand invocation + awaited `Engine::send` backpressure (FR-006; Wiring-point assumption).
- **`[const §XVII.1]`** Gate-A obligation for threading + error-semantics public surface.
- **`[arch §4.4]`** `fixpp::session::Application` reserved in the `session/` module — placement of `include/fixpp/session/application.hpp` (amended to list the canonical 7-method set incl. `onCreate`).
- **`[L-015-4]`** Session callback lifetime/drain contract (`stop()` → `close(terminal)` + join-before-registry-clear) — FR-012; SC-005.
- **`[L-015-3]`** `Application` path marked Phase-5 (the seam this slice wires) — Context.
- **`[FIX-SL §4.5.4]`** Rejecting invalid messages — session `Reject(35=3)` for `fromAdmin` reject (FR-005).
- **`[FIX50SP2] Infrastructure / Business Rejects`** (catalogue row A-014) `BusinessMessageReject(35=j)` — business-level reject for `fromApp` reject (FR-005; D4).
