# Data Model — C ABI engine surface — Feature B (050)

Phase 1 entities. "Real C++" rows are source-verified (`file:line`). The C-ABI surface is opaque-handle + value-PoD only; no C++ type crosses the boundary.

---

## E-1 — Handle catalogue (additions to Feature A's 5)

| C-ABI handle | Owning? | Backed by (engine-internal) | Created by | Invalidated by |
|---|---|---|---|---|
| `fixpp_engine_t` | **owning** | `struct CapiEngine { io_context; work_guard; worker_thread(s); unique_ptr<Engine>; shared_ptr<CapiApplication>; uint16_t consumer_minor; }` | `fixpp_engine_create` | `fixpp_engine_destroy` (idempotent, NULL-safe, never-throws — drives `Engine::stop()` **unconditionally** before `~Engine` even on a never-started engine, since the dtor asserts `stopped()`; New-P1) |
| `fixpp_session_t` | **non-owning** | a `SessionId` value + back-pointer to `CapiEngine` (resolves via `Engine::lookup`) | `fixpp_session_open` (= `register_session`) | `fixpp_session_close` returns → tag set DEAD |
| `fixpp_msg_t` (inbound) | non-owning | stack wrapper over the borrowed `MessageView<Index>&` | engine, at `fromApp` dispatch | callback return (dispatch-window lifetime, `[2i §4.6]`) |
| `fixpp_engine_config_t` | **owning** (builder) | a heap `EngineConfig` under construction | `fixpp_engine_config_create` | `fixpp_engine_config_destroy` (or consumed by `fixpp_engine_create`) |
| `fixpp_session_config_t` | **owning** (builder) | a heap `SessionConfig` under construction | `fixpp_session_config_create` | `fixpp_session_config_destroy` (or consumed by `fixpp_session_open`) |

The handle struct carries a `tag_` (live / dead / mismatched) so NULL → `FIXPP_ERR_NULL_HANDLE` and destroyed/corrupted → `FIXPP_ERR_INVALID_HANDLE` (reuses Feature A's distinct codes; FR-006).

**`fixpp_session_t` keying** (research D-1): the handle wraps the **`SessionId`** (`begin_string`/`sender_comp_id`/`target_comp_id`, `engine.hpp:38–92`, hashable, `SessionId::from_config`), NOT a raw `Session*`. Every session op resolves `lookup(id)` fresh → a `shared_ptr<Session>` keepalive (null until established). This is what makes `fixpp_session_open` returning before establishment safe.

---

## E-2 — Lifecycle state machine (C-ABI engine)

```
created ──fixpp_session_open×N──▶ registered ──fixpp_engine_start──▶ started ──(async)──▶ session established
   │                                  │                                  │                       │
   │                                  └── open-after-start → ERROR        │                fixpp_session_send / recv callback
   └── fixpp_engine_destroy ◀──────────────────────────────────────────── fixpp_session_close ◀─┘
                (co_await stop(); join threads; ~Engine)
```

- **created**: `CapiEngine` built (io_context + workers running an idle work-guard); `Engine` constructed but `start()` not called.
- **registered**: ≥1 `fixpp_session_open` (= `register_session`). `fixpp_session_open` after `started` → domain error (FR-004; C-ABI-enforced register-before-start, research D-1).
- **started**: `fixpp_engine_start` called once (`Engine::start()`); role loops spawned; `fixpp_engine_start` again → error (once-only).
- **established**: `fixpp_session_is_established` flips true (`lookup(id) != null && Session::is_open()`); only now is a send guaranteed to reach a live peer (initiator: logged on; pre-establish send → `session_invalid_state_for_send` (77)).

---

## E-3 — Config builders (opaque, ABI-stable)

`fixpp_session_config_*` and `fixpp_engine_config_*` are the **opaque builder** answer (clarify FR-014): create → typed setters → consumed at open/create → destroy. A future field = a new setter (no struct-layout break → Article X-safe). The **v1.0 setter set is the reachable subset** of the real configs (the full configs are large; expose what a C/Python consumer realistically sets — the rest take engine defaults):

**`fixpp_session_config_*` v1.0 setters** (subset of `SessionConfig`, `session_config.hpp:153+`):
- `set_comp_ids(sender, target)` + `set_begin_string(s)` → `sender_comp_id`/`target_comp_id`/`begin_string`
- `set_role(initiator|acceptor)` → `role`
- `set_heartbeat_seconds(n)` → `heartbeat_interval`
- `set_security(profile_kind, …)` → `security_profile` (plaintext / TLS — reuses 043/the `SecurityProfile` enum; null/unset → open() rejects, FR-018-parity)
- `set_dictionary(fixpp_dict_t*)` → `dictionary` (required when a dict handle exists; dictionary creation/loading is Feature C (`fixpp_dict_load_*`); Feature-B tests supply the dictionary via a test-only seam — `Session::open` rejects null with no engine fallback (`src/session/session.cpp:925-931`); see OQ-1 RESOLVED below — L-050-1)
- reset knobs (`set_reset_on_logon(bool)` etc.) as thin pass-throughs
- *Not exposed v1.0*: executor/clock/arena overrides, tap consumer, transport_send sink, logger/tracer overrides (engine-internal; take defaults).

**`fixpp_engine_config_*` v1.0 setters** (subset of `EngineConfig`, `engine_config.hpp:125+`):
- `set_worker_threads(n)` (default 1, research D-2)
- `set_realtime_clock()` (installs a real-time clock; clock is **required non-null** — `Engine::start` rejects null → `clock_not_set`) — name matches `contracts/config-builders.md` + `tasks.md`
- dictionaries / store-factory / transport-factory / cert-source defaults as needed (mostly engine defaults for v1.0)
- the engine application is set internally to the `CapiApplication` trampoline (NOT a consumer setter).

> **OQ-1 — RESOLVED at Gate A round 1 (2026-06-24): DESCOPE — test-supplied dictionary; round-trip blocked on Feature C (L-050-1).** A `SessionConfig` requires a non-null `dictionary` (`session_config.hpp:180`) and `Session::open()` **unconditionally** rejects null with **no engine fallback** (source-verified `session.cpp:925-931` — unlike the clock axis, there is NO `dictionary_override.value_or(engine_anchor)`). The earlier "engine-default dictionary the session inherits" resolution was **incorrect**: there is no engine-default-dictionary fallback in the C++ Session, and there is **no built-in/version-keyed dictionary factory** (a Gate-A Explore sweep confirmed the only `Dictionary` producers are C++ `XmlLoader::load(path)` / `load_from_string(xml)`; `version_registry` is a lookup over *pre-loaded* dictionaries, not a producer). The file-loading C-ABI surface (`fixpp_dict_load_*`) is **Feature C**. **Decision:** there is no cheap pure-C dictionary selector without pulling Feature C forward (or shipping a net-new embedded-dict-by-version mechanism — more than Simplicity First allows), so Feature B's round-trip uses a **test-supplied** dictionary (a test-built `Dictionary` injected behind the C-ABI seam) and productive pure-C dictionary loading is recorded as **L-050-1** (blocked on Feature C). SC-001 reworded to drop the "pure-C" over-claim. (Rejected: (a) pulling `fixpp_dict_load_*` forward = scope creep into C; (b) a net-new embedded-dict-by-version factory = net-new mechanism beyond "don't pull C forward".)

---

## E-4 — Reachable error-arm set through Feature-B producers (and what each one publishes)

The C++ `error` variants **reachable** through Feature-B producing functions (`engine_create`/`session_open`/`engine_start`/`session_send`/`session_close`), enumerated from the **implementation** (`src/capi/error.cpp:91-216` for the current code; `src/session/engine.cpp:1486-1595` `Engine::send` → `src/session/session.cpp:4023-4330` `Session::send`/`send_impl`; `src/session/session.cpp:878-1063` `Session::open`; `engine.cpp` `register_session`/`start`), not from a header comment. Each reachable arm carries an explicit, mechanically-checkable label: **existing** (already published by 049, code shown) vs **newly published by Feature B** (was UNKNOWN → a `FIXPP_ERR_SESSION_*`/app code).

The split is driven by the slot's block in `error.hpp` and its current `translate()` arm:
- The **session-PROTOCOL block** (slots 66-77, 116-121) and the **app block** (slots 129-131) are the arms 049 left at `UNKNOWN` (`error.cpp:128-145`, `:212-215`) — these are what L-049-2 named, and the reachable ones are what Feature B newly publishes.
- The **slot-47-55 threading block** lifecycle/config arms (`session_already_open`/`closed`, `invalid_session_config`, `executor_not_serialised`, `clock_not_set`) were **never at UNKNOWN** — 049 already published them to `THREAD_SESSION_LIFECYCLE` / `THREAD_CONFIG` (`error.cpp:92-104`). They were therefore **never in L-049-2's remit** (049 census-ground-truth scopes the session set to 66-77/116-121) and **stay on their existing THREAD code — they are NOT re-pointed to a SESSION block** (see `## Gate A → Round 2 — decisions` in `plan.md`).

| C++ `error` variant | slot | current `translate()` code (`error.cpp`) | Reached by | Disposition |
|---|---|---|---|---|
| `session_invalid_argument` | 119 | `UNKNOWN` (`:142`) | `register_session` dup / `send` id-not-registered (`engine.cpp:1544`) | **newly published by Feature B — was UNKNOWN — `FIXPP_ERR_SESSION_INVALID_ARGUMENT`** |
| `session_invalid_state_for_send` | 77 | `UNKNOWN` (`:138`) | `send` to a non-Active session (`engine.cpp:1516,1538,1557,1583` / `session.cpp:4033`) | **newly published by Feature B — was UNKNOWN — `FIXPP_ERR_SESSION_INVALID_STATE`** |
| `app_do_not_send` | 129 | `UNKNOWN` (`:212`) | `toApp` veto on `send` | **newly published by Feature B — was UNKNOWN — `FIXPP_ERR_APP_DO_NOT_SEND` (app block)** |
| `app_callback_threw` | 130 | `UNKNOWN` (`:213`) | `toApp` threw on `send` | **newly published by Feature B — was UNKNOWN — `FIXPP_ERR_APP_CALLBACK_THREW` (app block)** |
| `app_payload_malformed` | 131 | `UNKNOWN` (`:214`) | `send` with a malformed opaque frame (fail-closed validation pre-seqnum, `session.cpp:4112-4330`) | **newly published by Feature B — was UNKNOWN — `FIXPP_ERR_APP_PAYLOAD_MALFORMED` (app block)** |
| `session_already_closed` | 52 | `FIXPP_ERR_THREAD_SESSION_LIFECYCLE` (`:100-101`) | `fixpp_session_close` → `Session::close` on never-opened/already-closed (`session.cpp:1341`); **also `fixpp_session_send`** → `assign_outbound` `async_lock` fail when the session is terminating (TOCTOU vs a concurrent `close()` drain, `seqnum_manager.cpp:106`) | **existing, published by 049 — `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`** (threading block; LEAVE) |
| `wire_frame_too_large` | 30 | `FIXPP_ERR_WIRE_LIMIT_EXCEEDED` (`:70`) | `fixpp_session_send` → `Session::send_impl` builder-capacity / frame-size checks on an oversized opaque payload (>~3800 B into the 4096 B stack buffer; `session.cpp:4255,4279,4359,4367,4372,4376`) | **existing, published by 049 — `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`** (no re-point) |
| `invalid_session_config` | 53 | `FIXPP_ERR_THREAD_CONFIG` (`:102-104`) | `fixpp_session_open` → `register_session` `validate_inbound_messages && null dictionary` gate (`engine.cpp:210-211`) | **existing, published by 049 — `FIXPP_ERR_THREAD_CONFIG`** (threading block; LEAVE) |
| `clock_not_set` | 54 | `FIXPP_ERR_THREAD_CONFIG` (`:102-104`) | `fixpp_engine_start` → `start()` null clock | **existing, published by 049 — `FIXPP_ERR_THREAD_CONFIG`** (threading block; LEAVE) |
| `store_seqnum_overflow` | 60 | `FIXPP_ERR_STORE_RUNTIME` (`:109-112`) | `fixpp_session_send` → `assign_outbound` when the outbound counter is at `seqnum_max` (session-fatal, no wrap, I-8; `seqnum_manager.cpp:111-114`) | **existing, published by 049 — `FIXPP_ERR_STORE_RUNTIME`** (no re-point) |
| cancellation (`*_cancelled`/`dispatch_aborted`) | — | `FIXPP_ERR_CANCELLED` | send/close under total cancel | **existing, published by 049 — `FIXPP_ERR_CANCELLED`** (`[2i §4.9]` uniform) |

- **The `FIXPP_ERR_SESSION_*`/app codes Feature B newly publishes are exactly the 5 "was UNKNOWN" rows above** (`session_invalid_argument` 119, `session_invalid_state_for_send` 77, `app_do_not_send` 129, `app_callback_threw` 130, `app_payload_malformed` 131). These are the **only** arms re-pointed off `UNKNOWN`. **Numeric slots** taken from the reserved `[2i §4.3]` session/app blocks; exact values fixed in `error.h` + `error_codes_v1.txt` (introducing_minor = **3**).
- **A store *I/O persistence* failure (`store_io_failure` 56, `store_capacity_exhausted` 59) is NOT a reachable send return** — on the `Session::send` path the durable-before-transmit `store_then_emit` **swallows** store errors (`(void)store_r; // logged-then-proceed (I-07)`, `session.cpp:4790`); only `operation_aborted` propagates (as `dispatch_aborted` → `CANCELLED`). I-07 (logged-then-proceed) is an inherited engine invariant (007/024/029/032), not introduced by Feature B; a wrapper feature cannot make a persistence failure surface. The only **store-domain** code observable on `send` is `store_seqnum_overflow` (the in-memory counter-at-max check above), so AC3/US2 witness that arm — and the I/O-failure-swallow is recorded as **L-050-3** (round-1 E-4 mislabelled `store_io_failure` as the reachable send arm; corrected at Gate A round 3 — see `plan.md ## Gate A`).
- **`Session::open()`-path errors are NOT in the reachable table** — `session_already_open` (51, `session.cpp:887`) and `executor_not_serialised` (48, `session.cpp:1067` / `session_executor.cpp:42`, called from `Session::open` at `session.cpp:1081-1085`) are produced only **inside the async establishment loop** co_spawned by `start()`; they are **not surfaced as a synchronous `fixpp_*` return** — `fixpp_session_open` maps to `register_session` (not `Session::open`; D-1, open ≠ connected), and a C consumer observes establishment failure via the `fixpp_session_is_established` poll, not an error code. They keep their THREAD_* mapping per the LEAVE decision regardless (translate()-level pure mapping, asserted by 049's total-switch oracle), so dropping them from this *reachable* table orphans nothing (`plan.md ## Gate A → Round 2` LEAVE block still lists 51/48 as a mapping statement). Slot **76** `session_invalid_config` (a distinct enumerator from slot-53 `invalid_session_config`) has **zero producers in `src/`** (grep-verified — only the `error.cpp:137` translate arm references it), so it is unreachable and stays residual-UNKNOWN.
- **Precise-enumeration mutation oracle (D-11)** — for an **existing-published** row the test asserts that arm still maps to its **existing** code (`THREAD_SESSION_LIFECYCLE` / `THREAD_CONFIG` / `STORE_RUNTIME` / `CANCELLED`) at the `translate()` level (a pure mapping assertion — no producer needed), i.e. it was NOT silently re-pointed; for a **newly-published** row the test asserts the arm now maps to its specific new SESSION/app code and **flips off `UNKNOWN` → RED**. Neither is a "returns a published code" proxy (per [[feedback_completeness_gate_exact_set_not_subset]] / [[feedback_coverage_push_enshrines_bugs]]).
- **Stay UNKNOWN** (no reachable Feature-B producer): the remaining session-PROTOCOL `session_*` variants (slots 66-76, 116-118, 120-121) + `log_*`/`otel_*`/`out_of_memory` → residual L-049-2, documented. The threading-block arms above are NOT in this set — they were already published, not UNKNOWN.

---

## E-5 — Receive trampoline (`CapiApplication`)

```
struct CapiApplication : fixpp::session::Application {
  // map mutated pre-start (SINGLE_THREAD), read on-strand in fromApp
  std::unordered_map<SessionId, CallbackEntry> cbs_;   // {fixpp_recv_cb, void* userdata}
  expected_t<void> fromApp(MessageView<Index> const& m, SessionId const& id) override {
    auto it = cbs_.find(id);
    if (it == cbs_.end()) return {};                   // default-accept (no cb)
    fixpp_msg_t inbound = wrap_on_stack(m);            // borrowed, no heap (D-6)
    try {
      it->second.cb(&inbound, it->second.userdata);    // on the session strand
    } catch (...) {
      // on-strand steady-state invariant violation (a C fn ptr threw):
      // fatal-log + std::abort, NOT translated, NOT routed to fromApp reject.
      fatal_log_and_abort("receive callback threw");   // (send-and-receive.md exception policy)
    }
    invalidate(inbound);                               // dispatch-window lifetime ends (FR-012)
    return {};                                         // v1.0 void cb → always accept (D-4)
  }
};
```
- Installed as `EngineConfig::application` at `fixpp_engine_create`; outlives the Engine (held by `CapiEngine`).
- `fromApp` is on the per-session strand (proof: `include/fixpp/session/engine.hpp:180–189` + strand created at `engine.cpp:1163`, loop spawned on it at `engine.cpp:1182–1192`; NOT `application.hpp:67-68`, which says "engine's exec_ after FSM, INV-6") → callback reentrancy class `REQUIRES_SESSION_LOCK`; zero global-heap alloc (D-6).
- The callback runs on a **fixpp-owned worker thread** (D-2), not the consumer's — documented.

---

## E-6 — Cross-thread blocking bridges (close AND send)

There are **TWO** synchronous C-thread → io-worker blocking bridges, not one — both `fixpp_session_close` and `fixpp_session_send`:

- **`fixpp_session_send`**: `Engine::send` returns `asio::awaitable<expected_t<void>>` (`engine.hpp:266`); the v1.0 thunk **posts onto the io worker and blocks** for the `expected_t` result to return a `fixpp_error_t` (FR-008; the non-blocking `_async` form is the L-050 follow-up, research D-10). So send carries the **same** cross-thread post+wait TSan exposure as close, and is the **same** root of the D-10 callback deadlock (the blocking wrapper, not the awaitable). Its alloc/lifetime/cancellation reasoning is owed too: the `frame` buffer is borrowed read-only for the call (caller may free on return); cancellation surfaces as `FIXPP_ERR_CANCELLED`.
- **The multi-threaded TSan harness (D-11) must cover the send bridge, not just close.**

### Close bridge (the synchronous C-thread → strand block for close)

`fixpp_session_close(session, /*graceful default*/)`:
1. NULL/invalid handle check → code.
2. `lookup(id)` → `shared_ptr<Session>` (null → `session_already_closed`/invalid).
3. Post `Session::close(graceful)` onto the session's serialisation domain (the v1.0 on-strand precondition, `session.hpp:159–171`); the calling C thread blocks on a completion signal (e.g. a `std::promise`/`future` or a `std::binary_semaphore`) until the awaitable completes.
4. Translate the `expected_t` result; invalidate the `fixpp_session_t` (tag → DEAD).

This is the primary **TSan-gated** surface (cross-thread post + wait; the engine runs on the worker thread, the C consumer on its own). Multi-threaded harness mandatory per [[feedback_single_threaded_harness_masks_strand_races]]. The forceful path (`close_mode::terminal`, breaks a blocked idle read via the per-session cancel + transport close) is the `force` variant — v1.0 exposes graceful; force is a thin optional (fold or L-050-y).

---

## E-7 — Version surface

- `FIXPP_C_ABI_VERSION_MINOR` 2 → **3** (`version.h`); `fixpp_version()` PoD reflects 0.3.0; `fixpp_library_version()` unchanged track.
- `fixpp_engine_create(consumer_major, consumer_minor, …)`: `consumer_major != FIXPP_C_ABI_VERSION_MAJOR` (0) → `FIXPP_ERR_VERSION_MISMATCH` (5), no engine. Else record `consumer_minor` → `CapiEngine::consumer_minor` → every return-path code runs `translate_for_consumer(code, consumer_minor)` (D-9). New session codes (introducing_minor 3) downgrade to `UNKNOWN` for a `consumer_minor < 3`.
