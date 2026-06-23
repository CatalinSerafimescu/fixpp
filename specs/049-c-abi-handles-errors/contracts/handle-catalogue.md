# Contract — Handle catalogue (CA-001) + export macro

**Headers**: `include/fix/c_api/handles.h` (NEW), `include/fix/c_api/export.h` (NEW).

## `export.h` — `FIXPP_API_EXPORT` (research D-5)
- `_WIN32` + building the lib (`FIXPP_CAPI_BUILD`) → `__declspec(dllexport)`; `_WIN32` consuming → `__declspec(dllimport)`; POSIX → `__attribute__((visibility("default")))`; static consumer → empty.
- C-clean (no C++); included by `error.h`, `version.h`, and the future split headers.

## `handles.h` — opaque catalogue (`[2i §4.2]`)
Five incomplete forward typedefs (definitions engine-internal):
```c
typedef struct fixpp_engine  fixpp_engine_t;
typedef struct fixpp_session fixpp_session_t;
typedef struct fixpp_msg     fixpp_msg_t;
typedef struct fixpp_dict    fixpp_dict_t;
typedef struct fixpp_store   fixpp_store_t;
```

## Discipline documented now (no functions yet)
- **Destroy / invalidation discipline** (`[2i §4.2.1]`, per-handle — NOT uniform):
  - `fixpp_engine_t`, `fixpp_dict_t`, and outbound `fixpp_msg_t` have a future `fixpp_<h>_destroy`; destroy is **idempotent**, NULL-safe (NULL / already-destroyed → no-op), never throws; double-destroy is safe.
  - `fixpp_session_t` is **closed via the lifecycle** `fixpp_session_close(session)` (Feature B) — there is **no `fixpp_session_destroy`**; the handle invalidates once close returns.
  - `fixpp_store_t` has **no destroy at all** — it is a non-owning observer of a session-owned store and invalidates when its session closes (`[2e §6.7]` N1).
  - inbound `fixpp_msg_t` is engine-destroyed at parse-window close (no consumer destroy).
- **Null vs invalid**: handle-taking functions check NULL first → `FIXPP_ERR_NULL_HANDLE`(3); a destroyed/corrupted handle → `FIXPP_ERR_INVALID_HANDLE`(4). (Codes published by error-surface; the *enforcement* arrives with the functions in B/C.)
- **No C++ symbol leakage** (§X.2): the typedefs are opaque; nothing from `fixpp::` appears in the public header. nm gate stays green.

## Acceptance (SC-003)
- A **pure-C** TU includes `<fix/c_api.h>`, declares pointers of each handle type, and compiles+links (no C++ headers pulled).
- The nm `abi-golden` gate shows only `fixpp_*` exports (handles add no symbols — typedefs are compile-time only).
