# Quickstart — 051 C-ABI Feature C (pure-C message read + write round-trip)

A pure-C consumer (no C++ headers) building on Features A/B + C. Illustrative — exact signatures in `contracts/`.

```c
#include <fix/c_api.h>   /* umbrella: engine.h, session.h, message.h, error.h, version.h, decimal.h */

/* --- send-side (toApp) callback: inspect/veto outbound messages --------- */
static fixpp_toapp_verdict on_send(const fixpp_msg_t* out, void* ud) {
    const char* mt; size_t n;
    if (fixpp_msg_get_msg_type(out, &mt, &n) == FIXPP_ERR_OK && n == 1 && mt[0] == 'D') {
        int64_t qty;
        if (fixpp_msg_get_int(out, 38 /*OrderQty*/, &qty) == FIXPP_ERR_OK && qty > 1000000)
            return FIXPP_ERR_APP_DO_NOT_SEND;        /* veto oversized orders */
    }
    return FIXPP_ERR_OK;                             /* otherwise send */
}

/* --- receive (fromApp) callback: read inbound fields + groups ----------- */
static void on_recv(const fixpp_msg_t* msg, void* ud) {
    const char* sym; size_t n;
    if (fixpp_msg_get_string(msg, 55 /*Symbol*/, &sym, &n) == FIXPP_ERR_OK)
        printf("inbound Symbol=%.*s\n", (int)n, sym);   /* sym ALIASES the wire buffer */

    fixpp_decimal_t px;
    if (fixpp_msg_get_decimal(msg, 44 /*Price*/, &px) == FIXPP_ERR_OK) { /* exact PRICE */ }

    /* repeating group (e.g. NoLegs=555) */
    const fixpp_group_t* legs; size_t count;
    if (fixpp_msg_get_group(msg, 555, &legs, &count) == FIXPP_ERR_OK) {
        for (size_t i = 0; i < count; ++i) {
            const char* leg_sym; size_t ls;
            fixpp_group_get_field_string(legs, i, 600 /*LegSymbol*/, &leg_sym, &ls);
        }
    }
    /* msg + sym + legs are invalid once this callback returns; copy out what you keep. */
}

int main(void) {
    /* ... fixpp_engine_create / fixpp_session_open / register callbacks BEFORE start ... */
    fixpp_session_register_callback(sess, on_recv, NULL);        /* Feature B */
    fixpp_session_register_send_callback(sess, on_send, NULL);   /* Feature C (US6) */
    /* ... fixpp_engine_start; wait fixpp_session_is_established ... */

    /* --- construct + commit + send an outbound NewOrderSingle ("D") ------ */
    fixpp_msg_t* out = NULL;
    if (fixpp_msg_create_outbound(sess, "D", 1, &out) != FIXPP_ERR_OK) return 1;
    fixpp_msg_set_string(out, 11, "ORDER123", 8);   /* ClOrdID */
    fixpp_msg_set_string(out, 55, "ESZ5", 4);       /* Symbol  */
    fixpp_msg_set_int   (out, 38, 100);             /* OrderQty */
    fixpp_decimal_t price; fixpp_decimal_parse("4250.25", 7, &price);
    fixpp_msg_set_decimal(out, 44, price);          /* Price */

    /* outbound repeating group (NoLegs=555) */
    fixpp_group_builder_t* gb = NULL; fixpp_entry_t* e = NULL;
    fixpp_msg_group_begin(out, 555, &gb);
    fixpp_group_builder_add_entry(gb, &e); fixpp_entry_set_string(e, 600, "ES", 2);
    fixpp_group_builder_add_entry(gb, &e); fixpp_entry_set_string(e, 600, "NQ", 2);
    fixpp_msg_group_end(out, gb);

    const uint8_t* payload; size_t plen;
    if (fixpp_msg_commit(out, &payload, &plen) == FIXPP_ERR_OK) {
        fixpp_error_t rc = fixpp_session_send(sess, payload, plen);   /* Feature B send */
        /* rc == FIXPP_ERR_APP_DO_NOT_SEND if on_send vetoed;
           FIXPP_ERR_SESSION_INVALID_STATE if not yet established;
           FIXPP_ERR_OK on transmit. */
    }
    fixpp_msg_destroy(out);   /* idempotent; releases the arena slot */
    /* ... fixpp_session_close / fixpp_engine_destroy ... */
    return 0;
}
```

## Notes

- **Read pointers alias the wire buffer** — copy out anything you keep past the callback / past the next mutating set / past destroy. No `free()`.
- **Outbound is tombstoned on session close** — using `out` after the session closes returns `FIXPP_ERR_INVALID_HANDLE` (not UAF); `fixpp_msg_destroy` stays a safe no-op.
- **Cross-strand handoff** of an inbound message: `fixpp_msg_clone(msg, &copy)` first — the clone is owner-controlled, survives the dispatch window, and its reads are `FIXPP_THREAD_SAFE`.
- **Error legibility (US5):** a malformed payload → `FIXPP_ERR_APP_PAYLOAD_MALFORMED`; an unregistered session id → `FIXPP_ERR_SESSION_INVALID_ARGUMENT` — published codes, not the old `FIXPP_ERR_UNKNOWN`. `fixpp_strerror(rc)` gives a static description.
- The round-trip uses a **test-supplied dictionary** (inherited L-050-1) until the C-ABI dictionary loader ships.
