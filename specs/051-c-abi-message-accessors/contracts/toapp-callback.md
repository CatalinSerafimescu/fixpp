# Contract — Send (toApp) callback hook (Group 3, user decision 2026-06-24)

Header: `include/fix/c_api/session.h` (EDIT). Impl: `src/capi/session.cpp` (registration) + `src/capi/engine.cpp` (`CapiApplication::toApp` override). Mirrors the Feature-B `fromApp` receive trampoline.

## Registration (pre-start, SINGLE_THREAD)

```c
/* Verdict the callback returns to steer the originate path. CLOSED enum — NOT
   an alias of fixpp_error_t (so an accidental `return FIXPP_ERR_TAG_NOT_FOUND`
   cannot be a legal send/veto verdict and silently terminal-close the session).
   Fixed int constants for a stable C ABI. */
typedef enum {
    FIXPP_TOAPP_SEND  = 0,   /* proceed → transmit */
    FIXPP_TOAPP_VETO  = 1,   /* suppress → app_do_not_send */
    FIXPP_TOAPP_ERROR = 2    /* callback signalled failure → app_callback_threw */
} fixpp_toapp_verdict;

/* Invoked on the session strand BEFORE an application message is transmitted.
   `outbound` is a READ-ONLY *framed* fixpp_msg_t (US1 accessors apply; framing
   tags 8/9/34/49/52/56/10 ARE readable — see message-read.md "Framed toApp
   view"); valid only for the duration of the call. Returns a verdict. */
typedef fixpp_toapp_verdict (*fixpp_send_cb)(const fixpp_msg_t* outbound, void* userdata);

fixpp_error_t fixpp_session_register_send_callback(fixpp_session_t* session,
                                                   fixpp_send_cb cb, void* userdata);
```

## Verdict → `Application::toApp` mapping (D-8)

| C callback returns | `CapiApplication::toApp` returns | Effect | `fixpp_session_send` result |
|---|---|---|---|
| `FIXPP_TOAPP_SEND` (0) | `{}` | message transmitted | `FIXPP_ERR_OK` |
| `FIXPP_TOAPP_VETO` (1) | `unexpected(app_do_not_send)` | suppressed (DoNotSend) | `FIXPP_ERR_APP_DO_NOT_SEND` |
| `FIXPP_TOAPP_ERROR` (2) | `unexpected(app_callback_threw)` | terminal-close (per `session.hpp:455–464`) | `FIXPP_ERR_APP_CALLBACK_THREW` |
| **any out-of-range value** | `unexpected(app_callback_threw)` | **defined C-ABI-misuse path** → treated as ERROR, terminal-close (surfaced, NOT silently coerced to send) | `FIXPP_ERR_APP_CALLBACK_THREW` |

- The "error" verdict (and the out-of-range misuse path) MUST map to **exactly** `app_callback_threw` (not a generic error — `application.hpp:102` says `unexpected(other_error) ⇒ abort`; `app_callback_threw` instead triggers surfaced terminal-close).
- A C callback cannot throw a C++ exception → the throw-equivalent is the explicit `FIXPP_TOAPP_ERROR` verdict.
- No callback registered → `toApp` returns `{}` (default send), exactly today's behaviour.
- Reentrancy: `FIXPP_REQUIRES_SESSION_LOCK` (runs on `exec_`, `application.hpp:8`). The callback must not allocate on the global heap and must not call back into a blocking session API.
- Scope: **originate-path tap only** (ResendRequest retransmissions are not surfaced — L-019-4). `toAdmin` is **not** exposed (inspect-only in C++; no veto) — out of scope for v1.0.

Backing: `Session::send_impl` already fires `engine.application->toApp(mv, sid)` on the originate path (`session.cpp:278`, `:3523`); `CapiApplication` adds the `toApp` override routing to the slot's `send_cb` (data-model E-6).
