# Quickstart — driving a fixpp session from pure C (Feature B)

This is the SC-001 round-trip a C consumer performs (no C++ headers, links only the C ABI). Illustrative; exact spellings finalised at implement.

```c
#include <fix/c_api.h>   /* umbrella: engine.h + session.h + error.h + handles.h + version.h */

static void on_message(const fixpp_msg_t* inbound, void* userdata) {
    /* Runs on a fixpp-owned worker thread, ON the session strand.
       `inbound` is valid ONLY until this function returns — copy out what you need.
       Do not retain or destroy it. Keep your own state thread-safe.
       ⚠️ DO NOT call fixpp_session_send / _close / fixpp_engine_destroy from here —
       it deadlocks (D-10). To reply, enqueue and send from another thread. */
    /* (field access — fixpp_msg_get_* — is Feature C; here we just count) */
    (*(int*)userdata)++;
}

int main(void) {
    /* 0. compatibility check (Feature A) */
    fixpp_version_t v = fixpp_version();
    /* major must match what this header was compiled against */

    /* 1. engine config + create (records consumer ABI version for the downgrade) */
    fixpp_engine_config_t* ecfg = NULL;
    fixpp_engine_config_create(&ecfg);
    fixpp_engine_config_set_realtime_clock(ecfg);          /* clock is required */
    /* fixpp_engine_config_set_worker_threads(ecfg, 1); */ /* default */

    fixpp_engine_t* engine = NULL;
    fixpp_error_t rc = fixpp_engine_create(ecfg,
                          FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR,
                          &engine);
    if (rc != FIXPP_ERR_OK) { /* fixpp_strerror(rc) */ return 1; }

    /* 2. session config + open  (== register; BEFORE start) */
    fixpp_session_config_t* scfg = NULL;
    fixpp_session_config_create(&scfg);
    fixpp_session_config_set_comp_ids(scfg, "ME", "PEER");
    fixpp_session_config_set_begin_string(scfg, "FIX.4.4");
    fixpp_session_config_set_role(scfg, FIXPP_ROLE_INITIATOR);
    /* fixpp_session_config_set_dictionary(scfg, dict);  -- OQ-1: engine-default or test dict */
    /* fixpp_session_config_set_security(scfg, ...);     -- plaintext/TLS */

    fixpp_session_t* session = NULL;
    rc = fixpp_session_open(engine, scfg, &session);       /* registers the session */
    if (rc != FIXPP_ERR_OK) return 1;

    /* 3. register the receive callback (BEFORE start) */
    int received = 0;
    fixpp_session_register_callback(session, on_message, &received);

    /* 4. start the engine ONCE — now the role loops spawn and the session establishes async */
    rc = fixpp_engine_start(engine);
    if (rc != FIXPP_ERR_OK) return 1;

    /* 5. wait for establishment (open != connected) */
    bool up = false;
    while (!up) { fixpp_session_is_established(session, &up); /* sleep a bit */ }

    /* 6. send a committed wire frame (obtained from Feature C's fixpp_msg_commit,
          or a hand-rolled/golden frame for a Feature-B test) */
    const uint8_t* frame = /* ... */ 0; size_t len = /* ... */ 0;
    rc = fixpp_session_send(session, frame, len);
    /* rc: OK / APP_DO_NOT_SEND / SESSION_INVALID_STATE / CANCELLED ... */

    /* ... inbound messages arrive on on_message (worker thread) ... */

    /* 7. graceful close — handle invalidated on return */
    fixpp_session_close(session);

    /* 8. destroy the engine — co_awaits stop() + joins worker threads (idempotent, NULL-safe) */
    fixpp_engine_destroy(engine);
    return 0;
}
```

## Key semantics a consumer must know
- **open ≠ connected.** `fixpp_session_open` only registers; `fixpp_engine_start` (once, after all opens) spawns the loops; poll `fixpp_session_is_established` before sending.
- **Register callbacks before start.** v1.0 requires the callback map populated before `fixpp_engine_start` (D-4).
- **The callback runs on a fixpp worker thread, on the session strand**, and the `inbound` handle dies when the callback returns.
- **Never make a blocking C-ABI call (send/close/destroy) from inside the callback** — it deadlocks (D-10). Reply by enqueuing in the callback and sending from another thread.
- **Send takes bytes**, a committed wire frame — not a message handle. Outbound message *construction* is Feature C.
- **Error downgrade is live**: if you compiled against an older minor than the engine, codes newer than your minor arrive as `FIXPP_ERR_UNKNOWN`.

## Test surfaces (mirror of the SCs — strategy in research D-11)
- `send_recv_test` (**headline, two C-ABI engines over loopback plaintext TCP**) — a real bidirectional **conversation**: initiator A logs on to acceptor B → both poll `is_established` → A sends an app frame → B's on-strand callback receives it, copies out, and **replies from a drain thread** (the D-10 supported reply path, FR-013a) → A's callback receives the reply → both close gracefully → both destroy. Witnesses SC-001 round-trip + SC-008 (inbound invalid after return, ASan) + send-cancel → `CANCELLED`. Engine-default/test-built dictionary (OQ-1).
- `lifecycle_test` — create→open→start→is_established→close→destroy; open-after-start rejected; double-destroy no-op; **SC-007 close-breaks-a-blocked-idle-read** (real socket; witness = cancellation/socket-close, not deadline-elapse; TSan, multi-threaded).
- `error_block_test` — reachable session/app variants → published codes (**mutation-tested arms**); downgrade live: synthetic minor-3 code → UNKNOWN for a minor-2 consumer (SC-004/005).
- `thunk_split_test` — synthetic-throw: construction (create/open/start) → `*_CONFIG` no abort; steady-state (send) → abort via the §9-seam-5b SIGABRT trap (SC-006).
- pure-C smoke — headers compile as C, link only the C ABI, 0 C++ leak (SC-001/003).
- alloc guard — receive trampoline + send zero global-heap alloc under mallocnesia (D-6).
- D-10: **supported path only** (copy-out-then-send-from-a-drain-thread completes); the deadlock hazard is documented, not witnessed by a watchdog hang test.
