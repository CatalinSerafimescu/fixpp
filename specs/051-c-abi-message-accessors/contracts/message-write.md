# Contract — Outbound construct / set / commit + group build (CA-009 + CA-010-write)

Header: `include/fix/c_api/message.h` (NEW). Impl: `src/capi/message_write.cpp`. The outbound `fixpp_msg_t` is an in-arena accumulator (data-model E-3) bound to `Session::session_arena()`. `set_*` is zero-global-heap (arena deep-copy). Reentrancy `FIXPP_REQUIRES_SESSION_LOCK`; `fixpp_msg_destroy` `FIXPP_THREAD_SAFE`; `fixpp_msg_clone` `FIXPP_REQUIRES_SESSION_LOCK` on the source.

## Lifecycle

```c
fixpp_error_t fixpp_msg_create_outbound(fixpp_session_t* session,
                                        const char* msg_type, size_t msg_type_len,
                                        fixpp_msg_t** msg_out);
fixpp_error_t fixpp_msg_destroy(fixpp_msg_t* msg);                 /* idempotent, NULL-safe, never throws */
fixpp_error_t fixpp_msg_clone(const fixpp_msg_t* src, fixpp_msg_t** clone_out);
```

- `create_outbound`: `DICT_CONFIG` if `msg_type` absent from the session dictionary (D-5); `NULL_HANDLE` on NULL; `INVALID_HANDLE` on destroyed session. **Construction-time thunk** → catch→translate.
- `destroy`: releases the arena slot; no-op on NULL/destroyed/tombstoned (returns `OK`).
- `clone`: independent owner-controlled arena copy (session-independent; reads `THREAD_SAFE`; not session-tombstoned); `VERSION_MISMATCH` if src's resolved version not in loaded dicts; the v1.0 cross-strand-handoff escape hatch (seam #13).

## Setters (outbound only — inbound → `INVALID_HANDLE`, FR-007)

```c
fixpp_error_t fixpp_msg_set_string (fixpp_msg_t* msg, uint16_t tag, const char* value, size_t len);
fixpp_error_t fixpp_msg_set_bytes  (fixpp_msg_t* msg, uint16_t tag, const uint8_t* bytes, size_t len);
fixpp_error_t fixpp_msg_set_int    (fixpp_msg_t* msg, uint16_t tag, int64_t value);
fixpp_error_t fixpp_msg_set_double (fixpp_msg_t* msg, uint16_t tag, double  value);
fixpp_error_t fixpp_msg_set_decimal(fixpp_msg_t* msg, uint16_t tag, fixpp_decimal_t value);
fixpp_error_t fixpp_msg_remove_tag (fixpp_msg_t* msg, uint16_t tag);   /* idempotent */
```

- Deep-copies borrowed bytes into the arena (caller may free immediately). Overwrites an existing tag in place.
- **Rejects framing tags** `8/9/34/49/52/56/10` at set-time (INV-3) → a reject code (final code chosen in error-block-amendment.md; candidate `FIXPP_ERR_SESSION_INVALID_ARGUMENT`).
- `DICT_CONFIG` (tag absent from dict) / `TYPE_MISMATCH` (dict type ≠ flavour) / `WIRE_LIMIT_EXCEEDED` (obvious overrun). Setting invalidates prior `get_*` pointers on this msg.

## Commit (the app-payload bridge, D-3)

```c
fixpp_error_t fixpp_msg_commit(fixpp_msg_t* msg, const uint8_t** payload_out, size_t* len_out);
```

- Serialises the accumulator (E-3) into a **valid app-payload**: `35=<type>` first, fields in set-order, groups in grammar order, SOH-terminated, **no** framing tags. `*payload_out` aliases the arena; valid until the next mutation or destroy.
- The consumer ships it via the **existing, unchanged** `fixpp_session_send(session, *payload_out, *len_out)`.
- `WIRE_LIMIT_EXCEEDED` if the body exceeds the frame cap (~3800 B, aligned to `session.cpp:4021`). Unfinished group builder → reject. **Steady-state thunk** → abort on exception escape (`[2i §5.2]`).

## Group build (CA-010-write)

```c
typedef struct fixpp_group_builder fixpp_group_builder_t;
typedef struct fixpp_entry         fixpp_entry_t;

fixpp_error_t fixpp_msg_group_begin       (fixpp_msg_t* msg, uint16_t group_tag, fixpp_group_builder_t** b_out);
fixpp_error_t fixpp_group_builder_add_entry(fixpp_group_builder_t* b, fixpp_entry_t** entry_out);
fixpp_error_t fixpp_entry_set_string (fixpp_entry_t* e, uint16_t tag, const char* v, size_t len);
fixpp_error_t fixpp_entry_set_int    (fixpp_entry_t* e, uint16_t tag, int64_t v);
fixpp_error_t fixpp_entry_set_double (fixpp_entry_t* e, uint16_t tag, double v);
fixpp_error_t fixpp_entry_set_decimal(fixpp_entry_t* e, uint16_t tag, fixpp_decimal_t v);
fixpp_error_t fixpp_msg_group_end    (fixpp_msg_t* msg, fixpp_group_builder_t* b);
```

- `group_begin`: `TYPE_MISMATCH` if `group_tag` not a dictionary group; `INVALID_HANDLE` on inbound msg.
- `group_end`: seals; **invalidates** the builder + its entry handles (reuse → `INVALID_HANDLE`).
- Nested: an entry may `group_begin` a nested group (LIFO close).
