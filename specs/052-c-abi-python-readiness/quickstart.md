# Quickstart: the first fully-public-C-ABI live round-trip (052)

The happy path that is **impossible today** (needs the L-050-1/L-050-5 seams) and becomes possible with
this feature. This is the shape of the SC-001 witness (`tests/capi/public_roundtrip_test.cpp`) and the
template PY-001 binds. Uses ONLY `include/fix/c_api/` headers.

```c
#include <fix/c_api/engine.h>
#include <fix/c_api/session.h>
#include <fix/c_api/message.h>
#include <fix/c_api/dict.h>         /* NEW in 052 (aggregated by the <fix/c_api.h> umbrella, FR-014) */
#include <fix/c_api/error.h>

static void on_recv(const fixpp_msg_t* msg, void* ud) {
    /* NEW in 052: iterate every field without knowing tags a priori */
    size_t n = 0; fixpp_msg_field_count(msg, &n);
    for (size_t i = 0; i < n; ++i) {
        fixpp_msg_field_t f;
        if (fixpp_msg_field_at(msg, i, &f) == FIXPP_ERR_OK)
            printf("  %u = %.*s\n", f.tag, (int)f.len, f.value);
    }
}

int main(void) {
    /* 1. dictionary — NEW in 052: load from a bundled XML (no test seam) */
    fixpp_dict_t* dict = NULL;
    if (fixpp_dict_load_from_xml("dictionaries/FIX44.xml", &dict) != FIXPP_ERR_OK) return 1;

    /* 2. acceptor engine (engine A). The port-0 readback requires the acceptor BOUND before the
       initiator config is knowable, and fixpp_session_open is register-before-start — so the canonical
       SC-001 shape is TWO C-ABI engines, not one. */
    fixpp_engine_config_t* ec = NULL;
    fixpp_engine_config_create(&ec);
    fixpp_engine_config_set_realtime_clock(ec);
    fixpp_engine_t* acc_engine = NULL;
    fixpp_engine_create(ec, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &acc_engine);

    /* 3. acceptor (bind port 0 → OS-assigned) on engine A */
    fixpp_session_config_t* acc = NULL;
    fixpp_session_config_create(&acc);
    fixpp_session_config_set_comp_ids(acc, "ACC", "INI");
    fixpp_session_config_set_begin_string(acc, "FIX.4.4");
    fixpp_session_config_set_role(acc, FIXPP_ROLE_ACCEPTOR);
    fixpp_session_config_set_security(acc, FIXPP_SECURITY_INSECURE_PLAIN_TCP, NULL, NULL);
    fixpp_session_config_set_reset_seqnum_policy(acc, FIXPP_RESET_SEQNUM_BILATERAL_STRICT); /* NEW in 052 */
    fixpp_session_config_set_reset_on_logon(acc, true);          /* both sides reset */
    fixpp_session_config_set_dictionary(acc, dict);
    fixpp_session_config_set_tcp_endpoint(acc, "127.0.0.1", 0);  /* NEW in 052: bind, OS-assigned */
    fixpp_session_t* acc_s = NULL;
    fixpp_session_open(acc_engine, acc, &acc_s);
    fixpp_session_register_callback(acc_s, on_recv, NULL);

    fixpp_engine_start(acc_engine);                             /* acceptor must bind before we read */
    uint16_t port = 0;
    while (port == 0) fixpp_session_acceptor_bound_endpoint(acc_s, &port);  /* NEW in 052: readback */

    /* 4. initiator engine (engine B) → dial the acceptor's read-back port. SECOND engine, created and
       started only now that `port` is known. NOTE: fixpp_engine_create CONSUMES its engine-config (it
       deletes the builder), so engine B needs its OWN config — do not reuse `ec`. */
    fixpp_engine_config_t* ec2 = NULL;
    fixpp_engine_config_create(&ec2);
    fixpp_engine_config_set_realtime_clock(ec2);
    fixpp_engine_t* ini_engine = NULL;
    fixpp_engine_create(ec2, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &ini_engine);
    fixpp_session_config_t* ini = NULL;
    fixpp_session_config_create(&ini);
    fixpp_session_config_set_comp_ids(ini, "INI", "ACC");
    fixpp_session_config_set_begin_string(ini, "FIX.4.4");
    fixpp_session_config_set_role(ini, FIXPP_ROLE_INITIATOR);
    fixpp_session_config_set_security(ini, FIXPP_SECURITY_INSECURE_PLAIN_TCP, NULL, NULL);
    fixpp_session_config_set_reset_seqnum_policy(ini, FIXPP_RESET_SEQNUM_BILATERAL_STRICT); /* NEW in 052 */
    fixpp_session_config_set_reset_on_logon(ini, true);
    fixpp_session_config_set_dictionary(ini, dict);
    fixpp_session_config_set_tcp_endpoint(ini, "127.0.0.1", port); /* NEW in 052: dial read-back port */
    fixpp_session_t* ini_s = NULL;
    fixpp_session_open(ini_engine, ini, &ini_s);
    fixpp_engine_start(ini_engine);

    /* 5. once established, send an app message via the Feature-C outbound surface or a byte span;
       the acceptor's on_recv iterates it. */

    /* 6. teardown — both engines */
    fixpp_engine_destroy(ini_engine);
    fixpp_engine_destroy(acc_engine);
    fixpp_dict_destroy(dict);   /* NEW in 052 */
    return 0;
}
```

**What 052 adds (the only new calls above):** `fixpp_dict_load_from_xml` / `fixpp_dict_destroy`,
`fixpp_session_config_set_tcp_endpoint`, `fixpp_session_acceptor_bound_endpoint`,
`fixpp_session_config_set_reset_seqnum_policy`, and `fixpp_msg_field_count` / `fixpp_msg_field_at` — the
**7** new exported symbols. Everything else is the existing 049/050/051 surface.

**Reset policy (D-4, now pinned):** `fixpp_session_config_set_reset_seqnum_policy` ships in 052 (FR-005b),
so both sides set the policy explicitly through the public surface (here `BILATERAL_STRICT` +
`reset_on_logon(true)`; a consumer may instead select `BILATERAL_LENIENT`). The reviewed surface is a
deterministic 7 symbols — no +1 implement-time contingency.
