# Contract — Lifecycle surface (engine create/start/destroy + session open/close/is_established)

C declarations are illustrative shape (exact spelling fixed at implement); every symbol is `extern "C"`, `fixpp_*`, carries exactly one reentrancy class, and is on one side of the §5.2 thunk split.

```c
/* THREAD: SINGLE_THREAD · THUNK: construction-time (catch→*_CONFIG)
   `cfg` is CONSUMED (non-const): on success the built config is moved into the
   engine and the builder handle is invalidated (do NOT destroy it afterwards). */
fixpp_error_t fixpp_engine_create(fixpp_engine_config_t* cfg,
                                  uint16_t consumer_major, uint16_t consumer_minor,
                                  fixpp_engine_t** out_engine);

/* THREAD: SINGLE_THREAD · THUNK: construction-time */
fixpp_error_t fixpp_engine_start(fixpp_engine_t* engine);

/* THREAD: SINGLE_THREAD · idempotent · NULL-safe · never-throws */
void          fixpp_engine_destroy(fixpp_engine_t* engine);

/* THREAD: SINGLE_THREAD · THUNK: construction-time · MUST precede fixpp_engine_start
   `cfg` is CONSUMED (non-const): on success the built config is moved into the
   engine registry and the builder handle is invalidated (do NOT destroy it after). */
fixpp_error_t fixpp_session_open(fixpp_engine_t* engine,
                                 fixpp_session_config_t* cfg,
                                 fixpp_session_t** out_session);

/* THREAD: SINGLE_THREAD — non-callback / non-session-strand caller only; no
   concurrent close on the same handle (the thunk posts onto the session domain
   and BLOCKS, so a callback/strand caller deadlocks — FR-013a / D-10) · graceful */
fixpp_error_t fixpp_session_close(fixpp_session_t* session);

/* THREAD: THREAD_SAFE · O(1) lock-free (atomic reader snapshot) */
fixpp_error_t fixpp_session_is_established(fixpp_session_t* session, bool* out_established);
```

**Mapping & obligations**
- `fixpp_engine_create` → build `EngineConfig` from `cfg`; stand up internal io_context + worker thread(s) + work-guard (research D-2); install the `CapiApplication` trampoline as `EngineConfig::application`; `new Engine(exec, cfg)`; record `consumer_minor` (D-9). `consumer_major != FIXPP_C_ABI_VERSION_MAJOR` → `FIXPP_ERR_VERSION_MISMATCH` (no engine). Bad config (thrown/`expected` error) → domain `*_CONFIG` / `FIXPP_ERR_CAPI_CONFIG_INVALID`; no exception escapes.
- `fixpp_engine_start` → `Engine::start()` once; null clock → `clock_not_set` translated; second call → error. Loops spawn on the internal executor.
- `fixpp_engine_destroy` → **unconditionally** `co_await Engine::stop()` to completion on the worker (NOT guarded on "if started" — `~Engine` asserts `stopped_==true` which inits **false** at `engine.hpp:406` and is only set true by `stop()`; a created-but-never-started engine would `std::abort` in the dtor, violating FR-003 never-throws — e.g. `create` → `session_open` fails → `destroy`). `stop()` is idempotent from any state (`engine.hpp:199`), so the unconditional call is safe on the never-started path. Then reset work-guard; join worker thread(s); `delete Engine` (dtor asserts `stopped()` + zero outstanding `lookup` leases). NULL / double-destroy → no-op.
- `fixpp_session_open` → reject if engine already started (FR-004, C-ABI-enforced register-before-start); else `Engine::register_session(SessionConfig)`; on success allocate a `fixpp_session_t` keyed by `SessionId::from_config`; dup → `session_invalid_argument` (119) → `FIXPP_ERR_UNKNOWN` (publication deferred — L-050-4; the session/app block is not published). The `fixpp_session_config_t` is **consumed** here (non-const move into the registry) and invalidated on success; the caller must NOT `destroy` it afterwards. On failure the builder is untouched and the caller still owns it (must `destroy`).
- `fixpp_session_close` → `lookup(id)`; post `Session::close(graceful)` onto the session domain; block on completion (E-6); translate; invalidate the handle. Idempotent (already-closed → `session_already_closed` (52) → `FIXPP_ERR_THREAD_SESSION_LIFECYCLE`, the **existing** 049-published threading-block code — NOT a new SESSION code; see data-model E-4 LEAVE decision).
- `fixpp_session_is_established` → `*out = (lookup(id) != null && session->is_open())`; any-thread-safe.

**Handle discipline**: NULL → `FIXPP_ERR_NULL_HANDLE`; dead/corrupted → `FIXPP_ERR_INVALID_HANDLE` (Feature A codes; FR-006). `fixpp_session_t` is non-owning (no destroy; invalidated by close). `fixpp_engine_t` is owning (destroy). All return-path codes pass through `translate_for_consumer(code, consumer_minor)` (D-9) — except `fixpp_engine_create`'s own pre-engine `VERSION_MISMATCH`.
