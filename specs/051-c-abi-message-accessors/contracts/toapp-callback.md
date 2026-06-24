# Contract — Send (toApp) callback hook (Group 3, user decision 2026-06-24)

Header: `include/fix/c_api/session.h` (EDIT). Impl: `src/capi/session.cpp` (registration) + `src/capi/engine.cpp` (`CapiApplication::toApp` override). Mirrors the Feature-B `fromApp` receive trampoline.

## Registration (pre-start, SINGLE_THREAD)

```c
/* Verdict the callback returns to steer the originate path. */
typedef fixpp_error_t fixpp_toapp_verdict;   /* OK=send; APP_DO_NOT_SEND=veto; else=callback-threw */

/* Invoked on the session strand BEFORE an application message is transmitted.
   `outbound` is a READ-ONLY fixpp_msg_t (US1 accessors apply); valid only for
   the duration of the call. Returns a verdict (see below). */
typedef fixpp_toapp_verdict (*fixpp_send_cb)(const fixpp_msg_t* outbound, void* userdata);

fixpp_error_t fixpp_session_register_send_callback(fixpp_session_t* session,
                                                   fixpp_send_cb cb, void* userdata);
```

## Verdict → `Application::toApp` mapping (D-8)

| C callback returns | `CapiApplication::toApp` returns | Effect | `fixpp_session_send` result |
|---|---|---|---|
| `FIXPP_ERR_OK` | `{}` | message transmitted | `FIXPP_ERR_OK` |
| `FIXPP_ERR_APP_DO_NOT_SEND` | `unexpected(app_do_not_send)` | suppressed (DoNotSend) | `FIXPP_ERR_APP_DO_NOT_SEND` |
| any other value | `unexpected(app_callback_threw)` | terminal-close (per `session.hpp:455–464`) | `FIXPP_ERR_APP_CALLBACK_THREW` |

- The "error" verdict MUST map to **exactly** `app_callback_threw` (not a generic error — `application.hpp:102` says `unexpected(other_error) ⇒ abort`; `app_callback_threw` instead triggers surfaced terminal-close).
- A C callback cannot throw a C++ exception → the throw-equivalent is the explicit "error" verdict.
- No callback registered → `toApp` returns `{}` (default send), exactly today's behaviour.
- Reentrancy: `FIXPP_REQUIRES_SESSION_LOCK` (runs on `exec_`, `application.hpp:8`). The callback must not allocate on the global heap and must not call back into a blocking session API.
- Scope: **originate-path tap only** (ResendRequest retransmissions are not surfaced — L-019-4). `toAdmin` is **not** exposed (inspect-only in C++; no veto) — out of scope for v1.0.

Backing: `Session::send_impl` already fires `engine.application->toApp(mv, sid)` on the originate path (`session.cpp:278`, `:3523`); `CapiApplication` adds the `toApp` override routing to the slot's `send_cb` (data-model E-6).
