# Contract — Opaque config builders (FR-014)

Opaque builder handles so a future field is a new setter, not a struct-layout break (Article X). v1.0 exposes the **reachable subset** of the real configs; unset fields take engine defaults. Exact setter list finalised at /tasks (data-model E-3); the shape:

```c
/* engine config builder — THREAD: SINGLE_THREAD on each handle */
fixpp_error_t fixpp_engine_config_create(fixpp_engine_config_t** out);
fixpp_error_t fixpp_engine_config_set_worker_threads(fixpp_engine_config_t*, uint32_t n); /* default 1 */
fixpp_error_t fixpp_engine_config_set_realtime_clock(fixpp_engine_config_t*);             /* clock REQUIRED non-null */
/* ... dictionaries / store-factory / transport-factory defaults as needed (mostly engine defaults v1.0) ... */
void          fixpp_engine_config_destroy(fixpp_engine_config_t*);                        /* NULL-safe; or consumed by engine_create */

/* session config builder — THREAD: SINGLE_THREAD on each handle */
fixpp_error_t fixpp_session_config_create(fixpp_session_config_t** out);
fixpp_error_t fixpp_session_config_set_comp_ids(fixpp_session_config_t*, const char* sender, const char* target);
fixpp_error_t fixpp_session_config_set_begin_string(fixpp_session_config_t*, const char* s);
fixpp_error_t fixpp_session_config_set_role(fixpp_session_config_t*, fixpp_session_role role); /* initiator|acceptor */
fixpp_error_t fixpp_session_config_set_heartbeat_seconds(fixpp_session_config_t*, uint32_t n);
fixpp_error_t fixpp_session_config_set_security(fixpp_session_config_t*, /* profile kind + params */ ...);
fixpp_error_t fixpp_session_config_set_dictionary(fixpp_session_config_t*, fixpp_dict_t* dict);  /* OQ-1 */
/* ... reset knobs (set_reset_on_logon, ...) as thin pass-throughs ... */
void          fixpp_session_config_destroy(fixpp_session_config_t*);                     /* NULL-safe; or consumed by session_open */
```

**Obligations**
- Each builder handle wraps a heap `EngineConfig` / `SessionConfig` under construction (data-model E-1/E-3). `create` → `*_CONFIG`-flavoured construction-time thunk; setters validate eagerly where cheap (e.g. empty CompID → `FIXPP_ERR_SESSION_CONFIG`), else defer to open/create.
- The builder is **consumed** at `fixpp_engine_create` / `fixpp_session_open` (the thunk moves the built config into the engine/registry). After consumption the builder handle is invalidated; an explicit `destroy` before consumption frees it (idempotent, NULL-safe).
- **`set_security`**: reuses the existing `SecurityProfile` (plaintext_tcp from 043 / TLS from 2g). `kind::unset` → open() rejects (FR-018 parity); plaintext requires the explicit insecure opt-in.
- **OQ-1 — dictionary** (data-model E-3): a `SessionConfig` requires a non-null `dictionary`, but the `fixpp_dict_load_*` surface is Feature C. v1.0 resolution (at /tasks): prefer an **engine-default dictionary** the session inherits, OR gate the round-trip test on a test-built dictionary and document productive use needs Feature C. Do NOT pull a full dict-loader forward into B silently.
- The engine application is set internally to the `CapiApplication` trampoline — **not** a consumer-settable field.
- Every new builder symbol: appended to the nm golden (FR-018), reentrancy-annotated SINGLE_THREAD (FR-017), no exception escapes (FR-019).
