# Research — C ABI engine surface — Feature B (050)

Phase 0 decisions. Every "real surface" claim below is **source-verified against the actual C++ headers at plan time** (not inferred), per [[feedback_planning_explore_existence_claims_unreliable]]. Citations are `file:line`.

> **Path convention (cite hygiene, RC#5).** Shorthand `engine.hpp` / `session.hpp` / `application.hpp` / `engine_config.hpp` / `session_config.hpp` below resolve to `include/fixpp/session/<file>`; `engine.cpp` / `session.cpp` to `src/session/<file>`; `error.cpp` to `src/capi/error.cpp`; `version_registry.hpp` / `xml_loader.hpp` to `include/fixpp/dict/<file>`. Load-bearing cites are spelled out in full where re-pointed (D-3a, D-4).

---

## D-1 — The real engine is register-then-start-once; the C-ABI lifecycle adds `fixpp_engine_start` (DECIDED, user-ratified)

**Decision**: The C-ABI lifecycle is `fixpp_engine_create` → `fixpp_session_open`×N → `fixpp_engine_start` → drive → `fixpp_session_close` → `fixpp_engine_destroy`. `fixpp_session_open` maps to `Engine::register_session` (records config + role only); `fixpp_engine_start` maps to `Engine::start()` (once); the session establishes asynchronously after start. **open ≠ connected.**

**Evidence (verified)**:
- `Engine::register_session(SessionConfig)` — `engine.hpp:239` / impl `engine.cpp:206`: "Records config + role only; does NOT construct a Session (lazy)". Duplicate `SessionId::from_config(cfg)` → `session_invalid_argument` (119). **No before-start guard in the impl** (`engine.cpp:206–234` just inserts into `registry_`), but `start()` is "Legal to call once" (`engine.hpp:273`) and co_spawns loops only for sessions already registered → a registration after start is **functionally inert** (no loop spawned, `lookup` returns null forever). So the order constraint is real even though unenforced in C++.
- `Engine::start()` — `engine.hpp:277`: non-blocking, validates config (null clock → `clock_not_set`), co_spawns one connect loop per initiator + one accept loop per acceptor; "Legal to call once."
- `Engine::lookup(SessionId)` — `engine.hpp:301`: returns `shared_ptr<Session>`, **null** if not registered OR registered-but-not-established (acceptor with no peer, or loop not yet at `open()`); null is NOT an error.
- Real embedding (interop fixture `tests/interop/support/interop_fixture.hpp:35–77`): owns `asio::io_context ioc_` + `Engine engine_`; pattern = `register_session` → `start()` → pump `run_until` → `co_await stop()` before `~Engine`.

**Rationale**: A C consumer cannot supply an asio executor or pump an io_context, and cannot express "register all then start" without an explicit start verb. Auto-start-on-first-send breaks acceptor/receive-first sessions (nothing triggers start before the first inbound); single-session-per-engine drops multi-session. Explicit `fixpp_engine_start` is the honest 1-symbol mapping. The C-ABI **enforces** register-before-start (rejects `fixpp_session_open` after start with a domain error) to remove the C++ inert-registration footgun. **User-ratified at plan-time clarify (2026-06-24).**

**Alternatives rejected**: auto-start internally (breaks acceptors); single-session-per-engine (drops multi-session). Both recorded in spec Clarifications 2026-06-24.

---

## D-2 — The C-ABI engine owns an internal io_context + worker thread(s) (DECIDED)

**Decision**: `fixpp_engine_create` stands up an internal `asio::io_context` (or `thread_pool`) with a work guard + spawns N worker thread(s) running it; `Engine` is constructed with that executor. `fixpp_engine_destroy` `co_await`s `Engine::stop()` to completion, then resets the work guard, joins the worker thread(s), and destroys the `Engine` (whose dtor asserts `stopped()`).

**Evidence**: `engine.hpp:222` — "the engine owns NO worker threads (clarify-Q3). All loops co_spawn on exec; start() does NOT block or run the executor." `~Engine` `engine.cpp:132–152` asserts `stopped_` AND zero outstanding `lookup` leases (DEBUG). `Engine::stop()` (`engine.hpp:285`) is the idempotent total-cancellation teardown that closes transports and joins outstanding work before clearing the registry.

**Rationale**: the boundary must own the event loop because the C consumer has none. Worker-thread count: **1** for v1.0 (single-threaded executor → the session strand is the io thread; simplest, matches the test harness; multi-thread is a v1.x knob via the engine-config builder). Isolated to `engine.cpp`; the load-bearing novelty vs Feature A.

**Open implementation note**: the worker thread that runs `io_context` is where `fromApp` (and thus the receive callback) executes — so the callback runs on a fixpp-owned thread, NOT the consumer's thread. Documented in the contract + quickstart (the consumer's callback must be thread-safe w.r.t. its own state).

---

## D-3 — Send takes a committed wire-frame byte span, not a `fixpp_msg_t` (DECIDED, grounded)

**Decision**: `fixpp_session_send(session, const uint8_t* frame, size_t len)` maps to `Engine::send(SessionId, std::span<const std::byte>)`. The `fixpp_msg_t` does **not** appear in the send signature.

**Evidence**: `Engine::send` — `engine.hpp:266`: `asio::awaitable<expected_t<void>> send(SessionId const&, std::span<const std::byte> app_payload)`; any-thread-safe (enrollment gate + keepalive), runs `toApp` veto, delegates to `Session::send` (durable-before-transmit). `[2i §10]` line 113 (read directly): "v1.0 has only single-message commit (`fixpp_msg_commit` returns the serialised span; the consumer ships it via `fixpp_session_send`)."

**Rationale**: this is the engine's actual contract AND the published 2i model — not a choice. It **decouples Feature B from Feature C** (the outbound construction/commit surface): Feature B is testable with hand-rolled / golden wire frames. The handle/byte **asymmetry is intentional** and must be stated in the contract so it doesn't read as a bug: inbound = `fixpp_msg_t` (wraps `MessageView`), outbound = raw committed bytes.

**Durable-before-transmit (FR-009)** is honoured **by reference** — `Session::send` / `store_then_emit` (`session.hpp:1082`) stores before transmit; the C-ABI adds nothing, it just calls `Engine::send`.

---

## D-4 — Receive is an engine-wide `Application` trampoline holding a per-SessionId callback map (DECIDED)

**Decision**: `fixpp_engine_create` installs ONE internal C++ `Application` subclass as `EngineConfig::application`. It holds a `SessionId → {fixpp_recv_cb, void* userdata}` map. `fixpp_session_register_callback(session, cb, userdata)` populates the entry for that session's `SessionId`. `Application::fromApp(MessageView<Index>&, SessionId&)` looks up the entry and, if present, wraps the `MessageView` in a stack `fixpp_msg_t` and invokes the C callback; absent → default-accept (`return {}`).

**Evidence**: `Application` interface — `include/fixpp/session/application.hpp:46–108`: 7 virtuals, 0 pure; `fromApp(const MessageView<access_mode::Index>&, const SessionId&) → expected_t<void>` (`include/fixpp/session/application.hpp:84–88`, default accept). Threading (on-strand proof, **re-cited** — `application.hpp:67-68` itself only says callbacks run on "the engine's exec_ after the FSM accepts the frame (INV-6)", NOT the session strand): the per-session strand is created at `src/session/engine.cpp:1163` (`entry.session_strand.emplace(asio::make_strand(exec_))`), the role loop — "establish/handshake/read-pump/**callbacks**/sends/both teardown closes" — runs on it (`include/fixpp/session/engine.hpp:180–189`), and each loop is co_spawned ON that strand (`src/session/engine.cpp:1182–1192`). So `fromApp` (a callback in that loop) runs on the per-session strand; `application.hpp:67-68`'s "engine's exec_" is the executor that strand wraps. `Application` is engine-wide via `EngineConfig::application` (`engine_config.hpp:179`, null → no callbacks), MUST outlive the Engine + every Session.

**Rationale**: there is no per-session Application slot in C++; the engine-wide singleton + an internal SessionId-keyed map is the standard adapter. The map is mutated by `register_callback` (pre-start, SINGLE_THREAD) and read by `fromApp` (on-strand) — for v1.0 registration is required **before** `fixpp_engine_start` (no concurrent mutation vs read); post-start registration is a documented limitation (or a future strand-posted mutation). 

**Callback return**: the C callback is **void** for v1.0 (always-accept → `fromApp` returns `{}`). App-level reject-from-callback (mapping a callback status to `fromApp`'s `expected_t` reject) is a documented follow-up — keeps the v1.0 signature minimal. The inbound `fixpp_msg_t` lifetime is the dispatch window only (`[2i §4.6]`); the trampoline destroys/invalidates the stack handle on callback return (FR-012; ASan negative test SC-008).

---

## D-5 — `fixpp_session_close` posts onto the session domain and bridges back synchronously (DECIDED)

**Decision**: the close thunk resolves the `SessionId` → `Session` (`lookup`), then drives `Session::close(graceful)` **on the session's serialisation domain** (the v1.0 precondition), bridging the awaitable back to the synchronous C caller (post + block on a completion signal). On return the `fixpp_session_t` is invalidated (tag → dead).

**Evidence**: `Session::close(close_mode = graceful)` — `session.hpp:180`: awaitable, two-phase (graceful emits Logout under child cancellation; terminal skips phase-1), **three-state idempotent** (never-opened/closed → `session_already_closed`; already-closing → same in-flight awaitable; first → run+cache). `session.hpp:159–171` v1.0 precondition: "called from within session's serialization domain … Concurrent foreign-thread invocation = UNDEFINED in v1.0; C-ABI thunk (2i) responsible for serializing." Cancellation/transport-close to break a blocked read is the engine teardown machinery (`engine.hpp:280–285` stop closes transports; per-session `SessionEntry::session_cancel` is the per-session signal).

**Rationale**: the contract explicitly assigns the C-ABI thunk the job of serialising onto the session domain — so the thunk posts `Session::close` onto the strand and waits. This is **one of two** places a C thread blocks on the io thread (the other is the `fixpp_session_send` bridge — `Engine::send` is also an awaitable the thunk posts-and-blocks on; see data-model E-6). Close is the `FIXPP_SINGLE_THREAD` blocking pattern (reclassified at Gate A round 1 from `REQUIRES_SESSION_LOCK` — a callback/strand caller deadlocks, FR-013a) and a primary TSan-gated surface ([[feedback_single_threaded_harness_masks_strand_races]] / [[feedback_session_executor_lifetime...]]). `close_mode`: v1.0 exposes **graceful** as default; a `force` flag (→ `close_mode::terminal`) is a thin optional parameter (research note — fold if cheap, else L-050-y).

---

## D-6 — Steady-state hot path is allocation-free; trampoline guarded under mallocnesia (DECIDED)

**Decision**: the receive trampoline (`fromApp` → C callback) and the send steady-state thunk allocate **nothing on the global heap**; the inbound `fixpp_msg_t` is a **stack** wrapper over the borrowed `MessageView` (no heap). Verified under `mallocnesia` LD_PRELOAD on the round-trip (`[const §VIII.5]`, FR per §8). Per [[feedback_tracking_pmr_resource_false_pass]] the alloc gate uses global-malloc interception, not a tracking PMR.

**Evidence**: `[2i §8]` line 1564 — "zero new/delete between parse and fromApp … the C-ABI accessor hot path … MUST NOT malloc/new/PMR-allocate from the global heap." Inbound `MessageView<Index>` is borrowed (engine-owned, dispatch-window lifetime).

---

## D-7 — `fixpp_session_send` reentrancy class: `THREAD_SAFE` (RESOLVED at Gate A round 1)

**Tension**: `Engine::send` is **any-thread-safe** (`include/fixpp/session/engine.hpp:241–267`: enrollment gate + shared_ptr keepalive + re-entrant post). That licenses `fixpp_session_send` = `FIXPP_THREAD_SAFE`, materially more useful for a C consumer with no strand handle. But `[2i §4.10]` line 1176 **illustratively** lists `fixpp_session_send` under `FIXPP_REQUIRES_SESSION_LOCK`.

**Decision (Gate A round 1, 2026-06-24)**: annotate **`FIXPP_THREAD_SAFE`**, with an explicit carve-out forbidding the call from inside the receive callback (FR-013a/D-10 — the blocking C wrapper deadlocks there). This is a **recorded Gate-A deviation** from the `[2i §4.10]` send example, which is flagged **stale** (logged in plan `## Gate A`); `[2i]` itself is NOT reopened. The deviation is sound: the any-thread-ness is a property of `Engine::send`-the-awaitable, and the C consumer has no strand handle so `REQUIRES_SESSION_LOCK` would be unsatisfiable from a normal C thread.

---

## D-8 — `FIXPP_ERR_SESSION_*` block published now; discharges L-049-2 for reachable variants (DECIDED)

**Decision**: publish the real session-domain error block at its reserved `[2i §4.3]` slot; re-point the **reachable session-PROTOCOL + app** `translate()` arms that 049 left at `UNKNOWN` to the new codes; append to `error_codes_v1.txt` with **introducing_minor = 3**; pass the occupancy gate; amend `[2i §4.3]`. **Scope of the re-point (verified against the impl, not the header comment): exactly the 5 reachable arms 049 left at `UNKNOWN` — `session_invalid_argument` (119), `session_invalid_state_for_send` (77), `app_do_not_send` (129), `app_callback_threw` (130), `app_payload_malformed` (131).** The slot-47-55 **threading-block** lifecycle/config arms (`session_already_open` 51, `session_already_closed` 52, `invalid_session_config` 53, `executor_not_serialised` 48, `clock_not_set` 54) were **never at UNKNOWN** — 049 already publishes them to `THREAD_SESSION_LIFECYCLE` / `THREAD_CONFIG` — so they are outside L-049-2's remit and **stay on their existing THREAD code** (LEAVE decision, `plan.md ## Gate A → Round 2`). Session-PROTOCOL variants with **no** reachable producing path through `session_open`/`session_send`/`engine_*` stay `UNKNOWN` and remain documented (residual L-049-2).

**Evidence (impl-verified)**: Feature-A `translate()` (`src/capi/error.cpp`) maps the **session-PROTOCOL block** (slots 66-77, 116-121 — `error.cpp:128-145`), the **app block** (slots 129-131 — `:212-215`), and `log_*`/`otel_*`/`out_of_memory` → `FIXPP_ERR_UNKNOWN` as a deliberate override (049 data-model E-3 / L-049-2); the **threading block** (slots 47-55 — `:92-104`) is NOT at UNKNOWN (already published to `THREAD_*`). The producing functions, read from source: `Engine::send` (`engine.cpp:1486-1595`) → `Session::send`/`send_impl` (`session.cpp:4023-4330`) returns `session_invalid_state_for_send` (77), `session_invalid_argument` (119, `engine.cpp:1544`), `app_do_not_send` (129), `app_callback_threw` (130), **`app_payload_malformed` (131)** — the 020 fail-closed opaque-payload validation (`session.cpp:4112-4330`), reachable because the byte-span thunk ships a hand-rolled/committed frame — plus `wire_frame_too_large (30) → WIRE_LIMIT_EXCEEDED` (oversized opaque payload >~3800 B, `session.cpp:4255,4279,4359,4367,4372,4376`, existing), `store_seqnum_overflow (60) → STORE_RUNTIME` (outbound counter at `seqnum_max`, `assign_outbound`, `seqnum_manager.cpp:111-114`, existing), `session_already_closed (52) → THREAD_SESSION_LIFECYCLE` (also via `send`→`assign_outbound` `async_lock` fail — TOCTOU vs a concurrent `close()` drain, `seqnum_manager.cpp:106`, existing), and uniform `CANCELLED`. **A store *I/O persistence* failure (`store_io_failure` 56) is I-07-swallowed on the send path (`store_then_emit`, `session.cpp:4790` — `(void)store_r`) and does NOT surface as a `send` return (L-050-3); the round-1 enumeration mislabelled it send-reachable — corrected at Gate A round 3.**; `register_session` (called synchronously by `fixpp_session_open`) returns `session_invalid_argument` (119) and `invalid_session_config` (53, → THREAD_CONFIG, existing — the `validate_inbound_messages && null dictionary` gate, `engine.cpp:210-211`); `Session::close` (driven by `fixpp_session_close`) returns `session_already_closed` (52, existing THREAD_SESSION_LIFECYCLE); `start()` returns `clock_not_set` (54, existing). **`Session::open()`-path errors — `session_already_open` (51) and `executor_not_serialised` (48) — are produced only inside the async establishment loop (`session.cpp:887`; `session.cpp:1081-1085`→`session_executor.cpp:42`) and are NOT surfaced as a synchronous `fixpp_*` return** (open ≠ connected, D-1; the consumer polls `is_established`), so they are not Feature-B-reachable rows in E-4 — they retain their THREAD_* mapping by the LEAVE decision regardless. Slot **76** `session_invalid_config` (distinct from slot-53 `invalid_session_config`) has **zero producers** and stays residual-UNKNOWN. **data-model E-4 enumerates the exact reachable set** with per-row existing-vs-newly-published labels.

**Rationale**: these are now the producing functions, so the codes are observable — leaving them UNKNOWN would lose information at exactly the surface that needs it (FR-015). Additive at minor 0.3; the introducing_minor = 3 makes the downgrade (D-9) testable.

---

## D-9 — L-049-1 discharge: record `consumer_minor` at create, wire the live downgrade (DECIDED)

**Decision**: `fixpp_engine_create(consumer_major, consumer_minor, …)` records `consumer_minor` on the engine; every fallible C-ABI call on that engine applies Feature A's pure `translate_for_consumer(code, consumer_minor)` on the return path. Major mismatch (`consumer_major != engine_major`) → `FIXPP_ERR_VERSION_MISMATCH` (5) at create, no engine produced.

**Evidence**: Feature A shipped `fixpp_capi::detail::translate_for_consumer(fixpp_error_t, uint16_t consumer_minor) noexcept` as a pure, unit-tested function (049 contracts/error-surface.md; L-049-1 names the create-time recording as Feature B's). `[2i §4.5]` / `[2j:717]` engine-binding version-check protocol; `[2i §4.4]` downgrade direction.

**Rationale**: this is THE named obligation that makes CA-004's forward-compat downgrade live (FR-002). With D-8 introducing codes at minor 3, the end-to-end test (SC-004) is real: a minor-2 consumer creating an engine downgrades a minor-3 session code to `UNKNOWN`; a minor-3 consumer sees the real code.

---

## D-10 — Blocking C-ABI calls are FORBIDDEN from inside the receive callback (DECIDED — deadlock-avoidance contract)

**Decision**: the v1.0 contract states that the receive callback (`fixpp_recv_cb`) **MUST NOT** make a blocking C-ABI call on its own engine/session — specifically `fixpp_session_send`, `fixpp_session_close`, or `fixpp_engine_destroy`. The callback copies out what it needs and the consumer issues sends/closes **from a different (non-callback) thread** (e.g. after `fixpp_session_is_established`, or from a drain thread fed by the callback).

**Why (deadlock trace, confirmed by construction)**: the callback runs in `Application::fromApp` **on the session strand** = the single internal io worker thread (D-2 / E-5). A synchronous `fixpp_session_send` (or `fixpp_session_close`, E-6) thunk **posts** the awaitable onto that strand and **blocks** the calling thread until it completes. But the calling thread IS the strand's thread and the strand is occupied by the still-running callback → the posted work cannot run until the callback returns, which it cannot (blocked in the thunk). **Deadlock.** More worker threads do NOT help — a strand serialises regardless of pool size, so the posted continuation cannot run until the callback returns.

**Why it harmonises with the rest**: this is the same "copy out before returning" rule FR-012 already imposes (the inbound handle dies on callback return anyway). Sending from the consumer's OWN thread is safe: that thread ≠ the io thread, so the bridge's post runs on the (now-free) strand and the consumer thread unblocks normally. The classic receive→reply pattern is expressed as: callback enqueues → a consumer thread drains and replies.

**Note on the masking quote**: `Engine::send`'s "re-entrant on-strand calls are enqueued… no deadlock" (`engine.hpp:263`) is true of `Engine::send` *as an awaitable*; the **blocking C-ABI wrapper** around it reintroduces the deadlock. The contract (`send-and-receive.md`) flags this explicitly so the quote does not read as "handled."

**Follow-up (v1.x, not now)**: a non-blocking `fixpp_session_send_async` (detached co_spawn, fire-and-forget, no synchronous error return) would be callback-safe — recorded as a possible v1.x addition, NOT v1.0 (loses the durable-before-transmit error return; D-3). Tracked as part of L-050.

## D-11 — Test strategy (DECIDED, user-ratified 2026-06-24)

**Topology**: the **headline round-trip drives BOTH ends through the C ABI** — two C-ABI engines in one test process (initiator **A** + acceptor **B**), each owning its own internal io_context + worker (D-2), talking over **loopback plaintext TCP** (043 `insecure_plain_tcp`). This proves the genuine pure-C-consumer path, not a half-mock. Cheaper functional cases (lifecycle edges, error arms, thunk split) use the lighter **one-C-ABI-side + one-C++-side** `tests/interop/support` loopback harness where a full second engine is overkill. **No in-memory/mock transport** for the round-trip — a mock would hide SC-007 (a socket-close breaking a blocked `async_read`), exactly the class of bug [[feedback_engine_stop_must_close_transports_total_cancel_insufficient]] warns about.

**The conversational round-trip** (`send_recv_test`, the SC-001 + SC-008 witness): A(initiator) logs on to B(acceptor) → both poll `fixpp_session_is_established` → A `fixpp_session_send`s an app frame → B's on-strand callback receives it, copies it out, and **replies from a drain thread** (the D-10 supported reply path, FR-013a) → A's callback receives the reply → both `fixpp_session_close` gracefully → both `fixpp_engine_destroy`. One flow exercises lifecycle (both roles), send (both directions), receive callback (both ends), establishment, and graceful close.

**SC-007 (close breaks a blocked idle read)** — **required** (topology is real sockets): park a session in `async_read` (established, idle), `fixpp_session_close`, assert prompt teardown by *cancellation/socket-close*, NOT by a timeout elapsing (the witness must distinguish "close broke the read" from "the deadline fired"). TSan-gated (cross-thread close bridge, E-6) on a **multi-threaded** harness per [[feedback_single_threaded_harness_masks_strand_races]].

**D-10 deadlock** — **document + test the SUPPORTED path only** (copy-out-then-send-from-a-drain-thread completes). No watchdog "prove it hangs" test (fragile wedged-thread risk; marginal benefit with no real clients yet). The hazard is witnessed indirectly: the supported reply path completing IS the positive proof the contract works.

**Error-arm discipline**: every reachable `translate()` arm in E-4 is **mutation-tested** (a correctness oracle, not a "returns a published code" proxy), per [[feedback_completeness_gate_exact_set_not_subset]] / [[feedback_coverage_push_enshrines_bugs]]. The oracle is **per-row by disposition**: for a **newly-published** arm (the 5 "was UNKNOWN" rows — 119, 77, 129, 130, 131) the test asserts the arm now maps to its specific new SESSION/app code and that flipping it off `UNKNOWN` → RED; for an **existing-published** arm (the threading-block lifecycle/config arms, `store_*`, cancellation) the test asserts it still maps to its **existing** published code (`THREAD_SESSION_LIFECYCLE`/`THREAD_CONFIG`/`STORE_RUNTIME`/`CANCELLED`) — i.e. that Feature B did **not** silently re-point it — NOT a "flip off UNKNOWN" oracle, since these were never UNKNOWN. The downgrade (D-9) uses a synthetic minor-3 code vs a minor-2 consumer (SC-004).

**Thunk-split (SC-006)**: reuse Feature A's §9-seam-5b SIGABRT-trap fixture pattern (`sigaction` + `setjmp/longjmp`) for the steady-state send abort; construction-time (create/open/start) asserts `*_CONFIG` + no abort.

**OQ-1 resolution (dictionary) — DESCOPED (L-050-1)**: there is **no** engine-default-dictionary fallback (`Session::open` rejects null unconditionally, `session.cpp:925-931`) and no built-in dictionary factory; the round-trip uses a **test-supplied** dictionary (test-built `Dictionary` injected behind the C-ABI seam). Productive C-consumer dictionary *loading* (`fixpp_dict_load_*`) is **Feature C** — Feature B's round-trip is blocked on it. Do NOT pull a dict-loader forward into B (see data-model E-3 / D-3a).

**Cross-feature follow-up (user, 2026-06-24)**: hold a similar test-design conversation for **Feature C**; and/or plan a **comprehensive real-C-client conversational integration test after all three C-ABI features (A+B+C) are implemented** — a full pure-C program standing up a session, exchanging real business messages bidirectionally, and tearing down, as the end-to-end acceptance of the whole C-ABI surface. Tracked as **L-050 / a Feature-C+ planning item** (not a Feature-B blocker).

## D-3a — Dictionary is round-trip-blocked on Feature C (DECIDED at Gate A round 1 — L-050-1)

**Decision**: Feature B exposes **no pure-C path** to obtain or select a `Dictionary`; the round-trip uses a **test-supplied** dictionary and productive loading is blocked on Feature C (`fixpp_dict_load_*`).

**Evidence (source-verified at Gate A, Explore sweep)**: a `SessionConfig` requires non-null `dictionary` (`include/fixpp/session/session_config.hpp:180`) and `Session::open()` rejects null **unconditionally with no engine fallback** (`src/session/session.cpp:925-931` — the clock axis has `cfg_.clock_override ? … : engine_.clock`, but the dictionary axis has no equivalent). The only `Dictionary` producers are C++ `XmlLoader::load(path)` and `load_from_string(xml)` (`include/fixpp/dict/xml_loader.hpp`); there is **no built-in/version-keyed factory** — `version_registry` (`include/fixpp/dict/version_registry.hpp`) is a lookup over *pre-loaded* dictionaries, not a producer. Tests obtain a dictionary via `test_support::make_minimal_dictionary()` → `XmlLoader::load_from_string` (`tests/support/minimal_dictionary.hpp`).

**Rationale**: a cheap pure-C selector would require either pulling Feature C's loader forward or shipping a net-new embedded-dict-by-version mechanism — both beyond "don't pull Feature C forward" / Simplicity First. So SC-001 is descoped to "C-ABI round-trip with a test-supplied dictionary" and **L-050-1** records the round-trip block.

## D-12 — `fork()` after `fixpp_engine_create` is unsupported (DECIDED at Gate A round 1 — L-050-2)

**Decision**: document that a process holding a live `fixpp_engine_t` must not `fork()`; create the engine in the child after fork.

**Evidence/why**: `fixpp_engine_create` stands up an internal `io_context` + worker thread (D-2). POSIX `fork()` copies **only the calling thread**, so the child's worker thread does not exist — its `io_context` never runs, every session op hangs, and the destroy join deadlocks (`feedback_fork_inherited_asio_pool_deadlock`). A C consumer forking a server process is an ordinary pattern, so the limitation is documented before Gate A (spec Limitations L-050-2 + quickstart caveat). Not a code change for v1.0.

## Deferred / not in scope (recorded so the completeness audit doesn't read them as gaps)

- **`onLogon`/`onLogout`/`onCreate` lifecycle callbacks at the C boundary** — L-050-x; v1.0 ships only the `fixpp_session_is_established` poll accessor (FR-022).
- **App-level reject-from-receive-callback** (mapping a callback return to `fromApp`'s `expected_t` reject) — v1.0 callback is void/always-accept (D-4).
- **Multi-thread engine executor** (>1 worker) — v1.0 is single-threaded; a config-builder knob is a v1.x addition (D-2).
- **Post-start `register_callback` / `session_open`** — v1.0 requires registration before `fixpp_engine_start` (D-1/D-4).
- **`fixpp_session_send` THREAD_SAFE annotation** — RESOLVED to `FIXPP_THREAD_SAFE` at Gate A round 1 (D-7).
- **Outbound `fixpp_msg_*` construction/commit** — Feature C (D-3).
