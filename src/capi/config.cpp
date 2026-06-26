// SPDX-License-Identifier: AGPL-3.0-or-later
//
// src/capi/config.cpp — C-ABI opaque config builders (CA-005, T012)
// [2i §4.2] / specs/050-c-abi-session-send-recv/contracts/config-builders.md.
//
// Two opaque builder families wrap a heap EngineConfig/SessionConfig under
// construction (E-1/E-3). create = construction-time thunk (catch → *_CONFIG);
// setters validate cheaply (else defer to open/create); destroy is NULL-safe and
// never-throws. The builder is CONSUMED (moved into the engine/registry) by
// fixpp_engine_create / fixpp_session_open on success — see engine.cpp /
// session.cpp; on failure it is untouched and the caller still owns it.
// THREAD: SINGLE_THREAD per handle. All symbols here are construction-time.

#include "fix/c_api/engine.h"
#include "fix/c_api/session.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "capi_internal.hpp"

#include "fixpp/session/memory_store.hpp"
#include "fixpp/session/memory_store_factory.hpp"
#include "fixpp/session/security_profile.hpp"

extern "C" {

// ── Engine-config builder ───────────────────────────────────────────────────

fixpp_error_t fixpp_engine_config_create(fixpp_engine_config_t** out_cfg) {
    if (out_cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    *out_cfg = nullptr;
    try {
        *out_cfg = new fixpp_engine_config{};
        return FIXPP_ERR_OK;
    } catch (...) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
}

fixpp_error_t fixpp_engine_config_set_worker_threads(fixpp_engine_config_t* cfg, uint32_t n) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (n == 0) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
    cfg->worker_threads = n;
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_engine_config_set_realtime_clock(fixpp_engine_config_t* cfg) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    cfg->want_realtime_clock = true;
    return FIXPP_ERR_OK;
}

void fixpp_engine_config_destroy(fixpp_engine_config_t* cfg) {
    delete cfg;  // NULL-safe; never-throws (trivial members)
}

// ── Session-config builder ──────────────────────────────────────────────────

fixpp_error_t fixpp_session_config_create(fixpp_session_config_t** out_cfg) {
    if (out_cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    *out_cfg = nullptr;
    try {
        auto* h = new fixpp_session_config{};
        // Default in-memory store so a session is functional with no store
        // setter (v1.0 exposes none; data-model E-3 "engine defaults"). Use the
        // UNBOUNDED policy — a default bounded MemoryStore::Config exceeds the
        // 1 GiB per-session cap and would trip the storage-DoS guard at make()
        // (memory_store_factory.hpp:88-112 / EngineConfig comment).
        fixpp::session::MemoryStore::Config mcfg{};
        mcfg.policy = fixpp::session::capacity_policy::unbounded;
        h->cfg.store_factory = std::make_shared<fixpp::session::MemoryStoreFactory>(mcfg);
        *out_cfg = h;
        return FIXPP_ERR_OK;
    } catch (...) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
}

fixpp_error_t fixpp_session_config_set_comp_ids(fixpp_session_config_t* cfg, const char* sender,
                                                const char* target) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (sender == nullptr || target == nullptr || sender[0] == '\0' || target[0] == '\0') {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // both required (eager validation)
    }
    try {
        cfg->cfg.sender_comp_id = sender;
        cfg->cfg.target_comp_id = target;
    } catch (...) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_session_config_set_begin_string(fixpp_session_config_t* cfg,
                                                    const char* begin_string) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (begin_string == nullptr || begin_string[0] == '\0') {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
    try {
        cfg->cfg.begin_string = begin_string;
    } catch (...) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;
    }
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_session_config_set_role(fixpp_session_config_t* cfg, fixpp_session_role role) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    // A C caller can pass an out-of-range value (FFI bypass). A typed enum LOAD of
    // an out-of-range value is UB (-fsanitize=enum), so read the raw bytes and
    // switch on the integer; an out-of-range value falls through to CONFIG_INVALID.
    int raw = 0;
    static_assert(sizeof(role) <= sizeof(raw), "role wider than int");
    std::memcpy(&raw, &role, sizeof(role));
    switch (raw) {
        case FIXPP_ROLE_INITIATOR:
            cfg->cfg.role = fixpp::session::session_role::initiator;
            return FIXPP_ERR_OK;
        case FIXPP_ROLE_ACCEPTOR:
            cfg->cfg.role = fixpp::session::session_role::acceptor;
            return FIXPP_ERR_OK;
    }
    return FIXPP_ERR_CAPI_CONFIG_INVALID;  // out-of-range cast (FFI bypass)
}

fixpp_error_t fixpp_session_config_set_heartbeat_seconds(fixpp_session_config_t* cfg, uint32_t n) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    cfg->cfg.heartbeat_interval = std::chrono::seconds{n};
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_session_config_set_security(fixpp_session_config_t* cfg,
                                                fixpp_security_kind kind, const char* cert,
                                                const char* key) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    // Full TLS wiring (cert_source + transport factory) is NOT plumbed in
    // Feature B: the frozen 2-arg setter carries no CA-bundle path, so mtls_ca
    // cannot establish — selecting TLS sets the kind but Session::open() fails
    // closed (no TLS factory). The functional v1.0 path is the plaintext
    // round-trip; full TLS-over-C-ABI is a v1.x setter refinement (L-050-x).
    (void)cert;
    (void)key;
    // Out-of-range value (FFI bypass): a typed enum LOAD of an out-of-range value
    // is UB (-fsanitize=enum), so read the raw bytes and switch on the integer;
    // out-of-range falls through to CONFIG_INVALID.
    int raw = 0;
    static_assert(sizeof(kind) <= sizeof(raw), "kind wider than int");
    std::memcpy(&raw, &kind, sizeof(kind));
    switch (raw) {
        case FIXPP_SECURITY_TLS:
            cfg->cfg.security_profile.k = fixpp::session::SecurityProfile::kind::mtls_ca;
            return FIXPP_ERR_OK;
        case FIXPP_SECURITY_INSECURE_PLAIN_TCP:
            // Loud opt-in enumerator: confine the -Wdeprecated suppression here
            // (043 D-9 selection-site friction is satisfied at the C consumer's
            // explicit FIXPP_SECURITY_INSECURE_PLAIN_TCP choice).
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            cfg->cfg.security_profile.k =
                fixpp::session::SecurityProfile::kind::insecure_plain_tcp;
#pragma clang diagnostic pop
            return FIXPP_ERR_OK;
    }
    return FIXPP_ERR_CAPI_CONFIG_INVALID;  // out-of-range cast
}

fixpp_error_t fixpp_session_config_set_dictionary(fixpp_session_config_t* cfg, fixpp_dict_t* dict) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (dict == nullptr) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // null outer handle — existing mapping preserved
    }
    // Positive tag gate ([2i §4.2.2]): destroyed (DEAD) or wrong-type handles return
    // INVALID_HANDLE BEFORE the shared_ptr member is read.  A DEAD tag means dict was
    // already destroyed; any non-DICT tag means a mismatched handle was cast.
    const auto* h = reinterpret_cast<const fixpp_dict*>(dict);
    if (h->tag_ != FIXPP_HANDLE_TAG_DICT) {
        return FIXPP_ERR_INVALID_HANDLE;
    }
    if (h->dict == nullptr) {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // empty dict wrapper
    }
    cfg->cfg.dictionary = h->dict;  // thin pass-through (creation is Feature C / L-050-1)
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_session_config_set_reset_on_logon(fixpp_session_config_t* cfg,
                                                      bool reset_on_logon) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    cfg->cfg.reset_on_logon = reset_on_logon;
    return FIXPP_ERR_OK;
}

fixpp_error_t fixpp_session_config_set_reset_seqnum_policy(fixpp_session_config_t* cfg,
                                                            fixpp_reset_seqnum_policy kind) {
    if (cfg == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    // A C caller can pass an out-of-range value (FFI bypass). A typed enum LOAD of
    // an out-of-range value is UB (-fsanitize=enum), so read the raw bytes and
    // switch on the integer; an out-of-range value falls through to CONFIG_INVALID.
    int raw = 0;
    static_assert(sizeof(kind) <= sizeof(raw), "kind wider than int");
    std::memcpy(&raw, &kind, sizeof(kind));
    switch (raw) {
        case FIXPP_RESET_SEQNUM_BILATERAL_STRICT:
            cfg->cfg.reset_seqnum_policy_field =
                fixpp::session::reset_seqnum_policy::bilateral_strict;
            return FIXPP_ERR_OK;
        case FIXPP_RESET_SEQNUM_BILATERAL_LENIENT:
            cfg->cfg.reset_seqnum_policy_field =
                fixpp::session::reset_seqnum_policy::bilateral_lenient;
            return FIXPP_ERR_OK;
        case FIXPP_RESET_SEQNUM_UNILATERAL:
            cfg->cfg.reset_seqnum_policy_field = fixpp::session::reset_seqnum_policy::unilateral;
            return FIXPP_ERR_OK;
    }
    return FIXPP_ERR_CAPI_CONFIG_INVALID;  // out-of-range cast (FFI bypass)
}

fixpp_error_t fixpp_session_config_set_tcp_endpoint(fixpp_session_config_t* cfg, const char* host,
                                                     uint16_t port) {
    if (cfg == nullptr || host == nullptr) {
        return FIXPP_ERR_NULL_HANDLE;
    }
    if (host[0] == '\0') {
        return FIXPP_ERR_CAPI_CONFIG_INVALID;  // empty host (unusable)
    }
    // Steady-state thunk (spec.md FR-011): an escaping exception is an invariant
    // violation → fatal-log + abort, never translated.  Mirrors
    // fixpp_session_acceptor_bound_endpoint in session.cpp.
    try {
        // Mirror the L-050-5 seam (capi_loopback_support.hpp:67-68), now public:
        // set the reconnect_endpoint so the engine's auto-derived plaintext factory
        // can connect (initiator) or bind (acceptor), and install the transport_send
        // placeholder that the accept loop rebinds to the live socket.
        cfg->cfg.reconnect_endpoint = fixpp::transport::Endpoint{host, port};
        cfg->cfg.transport_send = [](std::span<const std::byte>) {};
    } catch (...) {
        std::fputs(
            "fixpp C-ABI: fixpp_session_config_set_tcp_endpoint caught an escaping exception; "
            "aborting (steady-state invariant violation, FR-011)\n",
            stderr);
        std::abort();
    }
    return FIXPP_ERR_OK;
}

void fixpp_session_config_destroy(fixpp_session_config_t* cfg) {
    delete cfg;  // NULL-safe; never-throws
}

}  // extern "C"
