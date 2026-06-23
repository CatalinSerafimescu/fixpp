# Data Model — C ABI engine surface — Feature B (050)

Phase 1 entities. "Real C++" rows are source-verified (`file:line`). The C-ABI surface is opaque-handle + value-PoD only; no C++ type crosses the boundary.

---

## E-1 — Handle catalogue (additions to Feature A's 5)

| C-ABI handle | Owning? | Backed by (engine-internal) | Created by | Invalidated by |
|---|---|---|---|---|
| `fixpp_engine_t` | **owning** | `struct CapiEngine { io_context; work_guard; worker_thread(s); unique_ptr<Engine>; shared_ptr<CapiApplication>; uint16_t consumer_minor; }` | `fixpp_engine_create` | `fixpp_engine_destroy` (idempotent, NULL-safe, never-throws) |
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
- `set_dictionary(fixpp_dict_t)` → `dictionary` (required; the `fixpp_dict_t` load surface is Feature C — for v1.0 a default/engine dictionary or a minimal loader; **flagged for /tasks**: a session needs a dictionary, and dict loading is Feature C → see Open Question OQ-1)
- reset knobs (`set_reset_on_logon(bool)` etc.) as thin pass-throughs
- *Not exposed v1.0*: executor/clock/arena overrides, tap consumer, transport_send sink, logger/tracer overrides (engine-internal; take defaults).

**`fixpp_engine_config_*` v1.0 setters** (subset of `EngineConfig`, `engine_config.hpp:125+`):
- `set_worker_threads(n)` (default 1, research D-2)
- `set_clock_default()` (a real-time clock; clock is **required non-null** — `Engine::start` rejects null → `clock_not_set`)
- dictionaries / store-factory / transport-factory / cert-source defaults as needed (mostly engine defaults for v1.0)
- the engine application is set internally to the `CapiApplication` trampoline (NOT a consumer setter).

> **OQ-1 (for /tasks + Gate A): the dictionary dependency.** A `SessionConfig` requires a non-null `dictionary` (`session.hpp` open() rejects null → `invalid_session_config`), but the `fixpp_dict_t` *loading* surface (`fixpp_dict_load_*`) is Feature C. v1.0 Feature B must give a session SOME dictionary. Options to resolve at /tasks: (a) a minimal `fixpp_dict_load_from_xml`-style entry pulled forward into B (scope creep); (b) an engine-default dictionary the session inherits; (c) gate the round-trip test on a test-built dictionary and document that productive use needs Feature C. Lean (b)/(c); do NOT pull (a) forward silently.

---

## E-4 — `FIXPP_ERR_SESSION_*` block membership (discharges L-049-2 for reachable variants)

The C++ `error` variants **reachable** through Feature-B producing functions (`engine_create`/`session_open`/`engine_start`/`session_send`/`session_close`), which therefore get **published codes** (re-pointed off Feature-A's `UNKNOWN`):

| C++ `error` variant | slot | Reached by | C-ABI code (new or existing) |
|---|---|---|---|
| `session_invalid_argument` | 119 | `register_session` dup / `send` id-not-registered | `FIXPP_ERR_SESSION_INVALID_ARGUMENT` (new) |
| `session_invalid_state_for_send` | 77 | `send` to a non-Active session | `FIXPP_ERR_SESSION_INVALID_STATE` (new) |
| `session_already_open` | 51 | second `open()` | `FIXPP_ERR_SESSION_ALREADY_OPEN` (new) |
| `session_already_closed` | — | close never-opened/closed | `FIXPP_ERR_SESSION_ALREADY_CLOSED` (new) |
| `invalid_session_config` | — | open/register bad config | `FIXPP_ERR_SESSION_CONFIG` (new; the construction-time `*_CONFIG` code for session) |
| `executor_not_serialised` | — | open with null engine executor | `FIXPP_ERR_SESSION_CONFIG` (coalesced) |
| `clock_not_set` | 54 | `start()` null clock | existing `FIXPP_ERR_*` (threading/cross-cutting block — already published in A) |
| `app_do_not_send` | 129 | `toApp` veto on send | `FIXPP_ERR_APP_DO_NOT_SEND` (new; app block) |
| `app_callback_threw` | — | `toApp` threw | `FIXPP_ERR_APP_CALLBACK_THREW` (new; app block) |
| cancellation (`*_cancelled`/`*_aborted`) | — | send/close under total cancel | `FIXPP_ERR_CANCELLED` (existing, `[2i §4.9]` uniform) |

- **Numeric slots** taken from the reserved `[2i §4.3]` session/app/control-plane blocks; exact values fixed in `error.h` + `error_codes_v1.txt` (introducing_minor = **3**). The precise enumeration is mutation-tested (the `translate()` arm for each must map to its specific code, not just "a published code" — per [[feedback_completeness_gate_exact_set_not_subset]] / [[feedback_coverage_push_enshrines_bugs]]).
- **Stay UNKNOWN** (no reachable Feature-B producer): the remaining `session_*`/`app_*` variants + `log_*`/`otel_*`/`out_of_memory` → residual L-049-2, documented.

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
    it->second.cb(&inbound, it->second.userdata);      // on the session strand
    invalidate(inbound);                               // dispatch-window lifetime ends (FR-012)
    return {};                                         // v1.0 void cb → always accept (D-4)
  }
};
```
- Installed as `EngineConfig::application` at `fixpp_engine_create`; outlives the Engine (held by `CapiEngine`).
- `fromApp` is on the session strand (`application.hpp:67–68`) → callback reentrancy class `REQUIRES_SESSION_LOCK`; zero global-heap alloc (D-6).
- The callback runs on a **fixpp-owned worker thread** (D-2), not the consumer's — documented.

---

## E-6 — Close bridge (the one synchronous C-thread → strand block)

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
