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
/* set_security: concrete typed signature (NOT a C variadic — a variadic with no
   fixed named parameter is not valid C and leaves the security shape unspecified).
   FROZEN enums (Gate-A pinned): fixpp_session_role { FIXPP_ROLE_INITIATOR=0,
   FIXPP_ROLE_ACCEPTOR=1 } and fixpp_security_kind { FIXPP_SECURITY_TLS=0,
   FIXPP_SECURITY_INSECURE_PLAIN_TCP=1 } (mirrors SecurityProfile::kind from 043/2g).
   `cert`/`key` are PEM file paths, ignored for the plaintext kind; plaintext requires
   the explicit insecure opt-in (`[const §XII.5]`). The broader setter *list* may still
   sequence at /tasks, but this signature + both enums are frozen now (P3 / Codex #5). */
fixpp_error_t fixpp_session_config_set_security(fixpp_session_config_t*,
                                                fixpp_security_kind kind,
                                                const char* cert, const char* key);
fixpp_error_t fixpp_session_config_set_dictionary(fixpp_session_config_t*, fixpp_dict_t* dict);  /* L-050-1: dict handle creation is Feature C — see Dictionary obligation below */
/* ... reset knobs (set_reset_on_logon, ...) as thin pass-throughs ... */
void          fixpp_session_config_destroy(fixpp_session_config_t*);                     /* NULL-safe; or consumed by session_open */
```

**Obligations**
- Each builder handle wraps a heap `EngineConfig` / `SessionConfig` under construction (data-model E-1/E-3). `create` → `*_CONFIG`-flavoured construction-time thunk; setters validate eagerly where cheap (e.g. empty CompID → `FIXPP_ERR_CAPI_CONFIG_INVALID` at the builder boundary), else defer to open/create — where a bad config surfaces the **existing** 049-published `invalid_session_config` (53) → `FIXPP_ERR_THREAD_CONFIG` (NOT a new SESSION code; see data-model E-4 LEAVE decision).
- The builder is **consumed** at `fixpp_engine_create` / `fixpp_session_open` — the create/open thunk takes the builder by a **non-`const`** pointer (a consuming move through a `const` pointer is incoherent C-API ownership; Codex #6) and moves the built config into the engine/registry. **On success** the builder handle is invalidated and the caller must NOT `destroy` it. **On failure** the builder is untouched and the caller still owns it (must `destroy`). An explicit `destroy` before consumption frees it (idempotent, NULL-safe).
- **`set_security`**: reuses the existing `SecurityProfile` (plaintext_tcp from 043 / TLS from 2g). The v1.0 collapsed enum maps **`FIXPP_SECURITY_TLS` (0) → `SecurityProfile::kind::mtls_ca`** (mutual-TLS, CA-validated; `cert`/`key` are the client cert + private-key PEM paths) and **`FIXPP_SECURITY_INSECURE_PLAIN_TCP` (1) → `insecure_plain_tcp`** (cert/key ignored; requires the explicit insecure opt-in, `[const §XII.5]`). The other C++ TLS sub-kinds (`mtls_pinned`, `one_way_ca`) are **not** selectable via the collapsed v1.0 enum — exposing them is a v1.x setter refinement (no new capability gap for Feature B, whose round-trip uses plaintext loopback). `kind::unset` → open() rejects (FR-018 parity).
- **Dictionary — DESCOPED (L-050-1)** (data-model E-3, research D-3a): a `SessionConfig` requires a non-null `dictionary` (`session_config.hpp:180`) and `Session::open()` unconditionally rejects null with **no engine fallback** (`session.cpp:925-931`). Source-verified at Gate A (Explore sweep): the **only** ways to obtain a `Dictionary` are `XmlLoader::load(path)` / `load_from_string(xml)` — both C++ constructs; there is **no built-in/version-keyed dictionary factory** (`version_registry` is a lookup over pre-loaded dictionaries, not a producer) and the file-loading C-ABI surface (`fixpp_dict_load_*`) is **Feature C**. Therefore there is **no pure-C path** for a consumer to obtain or select a dictionary in Feature B. `fixpp_session_config_set_dictionary` takes a `fixpp_dict_t*` whose creation is Feature C; for the Feature-B round-trip test the dictionary is supplied via a **test-only** path (a test-built `Dictionary` injected behind the C-ABI seam). Productive C-consumer dictionary loading is **blocked on Feature C** (`fixpp_dict_load_*`). We do NOT pull a dict-loader (or a net-new embedded-dict-by-version mechanism) forward into B (Simplicity First). SC-001 is reworded accordingly to "C-ABI round-trip with a test-supplied dictionary" (spec §SC-001).
- The engine application is set internally to the `CapiApplication` trampoline — **not** a consumer-settable field.
- Every new builder symbol: appended to the nm golden (FR-018), reentrancy-annotated SINGLE_THREAD (FR-017), no exception escapes (FR-019).
