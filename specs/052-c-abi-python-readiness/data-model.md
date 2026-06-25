# Data Model: C-ABI Python-readiness (052)

Entities/types the feature introduces or newly exposes. All additive; no shipped type changes.

## E-1 — `fixpp_dict_t` (newly *constructible*, not new)

The opaque handle already exists (`handles.h`); this feature makes it **constructible from pure C** for
the first time and adds the `tag_` liveness token the public destroy needs (the seam-only struct at
`src/capi/capi_internal.hpp:134` had none). Internal shape:

```cpp
struct fixpp_dict {
    std::uint32_t tag_ = FIXPP_HANDLE_TAG_DICT;  // NEW — liveness token, FIRST member (the
                                                 // §4.2.2 type-tag check must fire before any
                                                 // field deref); rewritten to FIXPP_HANDLE_TAG_DEAD
                                                 // on destroy. A new FIXPP_HANDLE_TAG_DICT live
                                                 // constant joins TAG_ENGINE/TAG_MSG (capi_internal.hpp:145).
    std::shared_ptr<const fixpp::dict::Dictionary> dict;
};
```

`tag_` is the **first** member to match the `fixpp_engine` (`:538`) / `fixpp_msg` (`:222`)
convention — a mis-typed pointer (a `fixpp_session_t*` passed as `fixpp_dict_t*`, §9 seam #12) then
reads the tag at a fixed offset without an out-of-bounds field dereference.

- **Ownership:** owning, refcounted via `shared_ptr<const Dictionary>` (`[2i]` line 1344). `fixpp_dict_load_from_xml`
  mints one; `fixpp_session_config_set_dictionary` copies the `shared_ptr` (existing thin pass-through,
  `config.cpp:189`); `fixpp_dict_destroy` releases the consumer's reference. The Dictionary's storage
  survives while any session still references it.
- **Lifecycle:** `load_from_xml` (mints a live `tag_`) → (optional `set_dictionary` copies the shared_ptr)
  → `destroy` (checks `tag_`, rewrites it to `FIXPP_HANDLE_TAG_DEAD`, retains a bounded dead shell).
  Destroy is NULL-safe and **idempotent double-destroy-safe via the tombstone** (per `[2i §4.2.1]`). The
  tombstone *mechanism* mirrors `fixpp_engine_destroy` (tag→DEAD + retained shell; dicts are O(few) →
  bounded); but because `[2i §4.2.1]` (line 415) mandates every `*_destroy` be thread-safe, the symbol
  stays `FIXPP_THREAD_SAFE` and a process-global mutex MUST cover the **entire critical section as one
  atomic unit** — `{ check tag_ != DEAD, release the shared_ptr ref, rewrite tag_ = DEAD, insert into the
  bounded dead-shell registry }` — NOT just the registry insert (the race is on the non-atomic
  `tag_`/`shared_ptr`, which sit OUTSIDE the registry); concurrent same-pointer destroy is thereby
  serialized (the second caller sees `tag_ == DEAD` under the lock and no-ops) — whereas
  `fixpp_engine_destroy`'s registry is unsynchronized under its as-built `FIXPP_SINGLE_THREAD` annotation.
  NOT the single-destroy-only `fixpp_msg_destroy` discipline.
- **Validation:** `load_from_xml` returns `CAPI_CONFIG_INVALID` if `XmlLoader::load` throws (bad path /
  malformed XML / unknown version / OOM), `NULL_HANDLE` on NULL `path`/`out`, and always sets
  `*out = NULL` on failure.

## E-2 — TCP endpoint configuration (a session-config field, not a handle)

No new type. `fixpp_session_config_set_tcp_endpoint(cfg, host, port)` writes two existing
`SessionConfig` fields:

- `reconnect_endpoint = fixpp::transport::Endpoint{host, port}` — initiator peer endpoint / acceptor
  bind endpoint (`session_config.hpp`).
- `transport_send` = an internal no-op placeholder (`session_config.hpp:217`), set by the C-ABI layer
  (NOT the consumer); the engine's auto-derived plaintext factory replaces it at connect/accept
  (`engine.cpp:1024`).

`fixpp_session_acceptor_bound_endpoint(session, port_out)` reads back
`Engine::acceptor_bound_endpoint(id).port` (`engine.hpp:312`) — `uint16_t` only (no host out-param; the
bind host is consumer-known — avoids a string-lifetime contract). `*port_out == 0` (with `OK`) until the
listener binds; the consumer polls.

- **Validation:** `set_tcp_endpoint` → `NULL_HANDLE` (NULL cfg/host), `CAPI_CONFIG_INVALID` (empty/
  unusable host). `acceptor_bound_endpoint` → `NULL_HANDLE` (NULL session/out), `INVALID_HANDLE`
  (destroyed session).
- **Explicitly NOT modeled:** `fixpp_endpoint_t` PoD, `fixpp_transport_t`/listener/factory handles,
  `reconnect_policy`/`connect_info` — all stay deferred to v1.x (`[2i §7.8]`); only primitives cross.

## E-3 — `fixpp_msg_field_t` (NEW PoD field view)

```c
typedef struct fixpp_msg_field {
    uint16_t        tag;     /* FIX tag number */
    const uint8_t  *value;   /* aliases the wire buffer; NOT NUL-terminated */
    size_t          len;     /* value byte length */
} fixpp_msg_field_t;
```

- **Source:** the inbound message's `OffsetTable` entry `{tag, offset, length}` (`offset_table.hpp:85`),
  resolved to `value = wire_base + offset`, `len = length` (D-1).
- **Enumeration set:** **every `OffsetTable` entry in wire/document order** — one entry per wire
  occurrence (a multiset; a tag repeated across N group instances yields N entries), including session
  header/trailer framing tags (8/9/35/49/56/34/52/10) when the inbound view holds them. The scalar typed
  getters read a first-occurrence **subset** (one-directional FR-007) — NOT a bi-directional set-equality.
- **Lifetime:** `value` aliases the wire buffer; valid for the parent `fixpp_msg_t` handle's **own**
  lifetime — the receive-callback dispatch window for an inbound message, or until `fixpp_msg_destroy` for
  a 051 view-backed clone (the clone's `view` points into its own `owned_view_`). Using it after that
  window is a documented use-after-invalidation (`[2i §4.6]`), not enforced on the hot path. **Zero
  global-heap** — no copy.
- **Access:** `fixpp_msg_field_count(msg, &count)`; `fixpp_msg_field_at(msg, i, &field)` for `i ∈
  [0,count)`; `i >= count` → `INDEX_OUT_OF_RANGE`. Over any view-backed handle (inbound parsed messages +
  051 clones); outbound accumulator iteration is unspecified / `INVALID_HANDLE` in v1.0.

## E-4 — Reset-seqnum policy setter (SHIPPED — pinned at Gate A r1, user decision)

Ships in this feature (FR-005b) as the **7th** exported symbol. Previously a D-4 +1 contingency; now
**pinned preemptively** so the reviewed ABI surface is deterministic at the `0→1` freeze (Codex P2#3 /
Opus N-B):

```c
typedef enum fixpp_reset_seqnum_policy {
    FIXPP_RESET_SEQNUM_BILATERAL_STRICT  = 0,  /* production default */
    FIXPP_RESET_SEQNUM_BILATERAL_LENIENT = 1,
    FIXPP_RESET_SEQNUM_UNILATERAL        = 2
} fixpp_reset_seqnum_policy;
fixpp_error_t fixpp_session_config_set_reset_seqnum_policy(
    fixpp_session_config_t* cfg, fixpp_reset_seqnum_policy kind);  /* SINGLE_THREAD */
```

The enumerator values mirror the C++ `enum class reset_seqnum_policy : std::uint8_t`
(`session_config.hpp:92-95`, STRICT=0 / LENIENT=1 / UNILATERAL=2 — verified) and map to
`SessionConfig::reset_seqnum_policy_field`. **Validation:** `NULL_HANDLE` (NULL cfg),
`CAPI_CONFIG_INVALID` (out-of-range enum). Header `session.h`; reentrancy `SINGLE_THREAD` (config setter);
golden +1 (→7 total); reentrancy-gate +1. SC-001 sets it explicitly through the public surface.
