# Contract — Outbound construct / set / commit + group build (CA-009 + CA-010-write)

Header: `include/fix/c_api/message.h` (NEW). Impl: `src/capi/message_write.cpp`. The outbound `fixpp_msg_t` is an accumulator (data-model E-3) in a **per-message `std::pmr::monotonic_buffer_resource` owned by the heap shell** (impl reconciliation — `Session::session_arena()` does not exist for the C-ABI path; see data-model E-3/E-9). `set_*` and `commit` are **zero-global-heap** (allocate from the pre-seeded per-message arena; SC-003 dual gate). Reentrancy `FIXPP_REQUIRES_SESSION_LOCK`; `fixpp_msg_destroy` `FIXPP_THREAD_SAFE`; `fixpp_msg_clone` `FIXPP_REQUIRES_SESSION_LOCK` on the source.

## Lifecycle

```c
fixpp_error_t fixpp_msg_create_outbound(fixpp_session_t* session,
                                        const char* msg_type, size_t msg_type_len,
                                        fixpp_msg_t** msg_out);
fixpp_error_t fixpp_msg_destroy(fixpp_msg_t* msg);                 /* NULL-safe + single-destroy (double-destroy same ptr = UB); never throws */
fixpp_error_t fixpp_msg_clone(const fixpp_msg_t* src, fixpp_msg_t** clone_out);
```

- `create_outbound`: `DICT_CONFIG` if `msg_type` absent from the session dictionary (D-5); `NULL_HANDLE` on NULL; `INVALID_HANDLE` on destroyed session. **Construction-time thunk** → catch→translate.
- `destroy`: frees the message shell + its per-message arena; NULL → `OK`. A double-destroy of the same non-null pointer is **UB** (free-on-destroy, B-051-2 — the consumer nulls its pointer after destroy). Destroy on a tombstoned (session-closed) handle is a single destroy and returns `OK`.
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
- **Rejects framing tags** `8/9/34/49/52/56/10` at set-time (INV-3) → **`FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN`** (1405, error-block-amendment.md) — a distinct **message-construction** reject, NOT the session-domain `SESSION_INVALID_ARGUMENT`.
- `DICT_CONFIG` (tag absent from dict) / `TYPE_MISMATCH` (dict type ≠ flavour) / `WIRE_LIMIT_EXCEEDED` (obvious overrun). Setting invalidates prior `get_*` pointers on this msg.

## Commit (the app-payload bridge, D-3)

```c
fixpp_error_t fixpp_msg_commit(fixpp_msg_t* msg, const uint8_t** payload_out, size_t* len_out);
```

- Serialises the accumulator (E-3) into a **valid app-payload**: `35=<type>` first, fields in set-order, groups in grammar order, SOH-terminated, **no** framing tags. `*payload_out` aliases the arena; valid until the next mutation or destroy.
- The consumer ships it via the **existing, unchanged** `fixpp_session_send(session, *payload_out, *len_out)`.
- **NORMATIVE inherited ordering invariant (Codex #4).** The committed span may be destroyed immediately after `fixpp_session_send` returns (the quickstart `commit → send → destroy` pattern) **only because** Feature B's `fixpp_session_send` blocks on `fut.get()` (`src/capi/session.cpp:216–218`) AND `Engine::send` deep-copies the payload at send entry (`src/session/engine.cpp:1490`), before any async hop. 051 **depends** on this ordering; a future non-blocking send or zero-copy `Engine::send` would turn the immediate-destroy into a UAF. FR-021 adds a commit→send→immediate-destroy ASan regression seam.
- `WIRE_LIMIT_EXCEEDED` if the body exceeds the frame cap (~3800 B, aligned to `session.cpp:4021`). `fixpp_msg_commit` called with an **open (unended) group builder** (a `fixpp_msg_group_begin`/`fixpp_entry_group_begin` whose matching `fixpp_msg_group_end` has not been called) → **`FIXPP_ERR_INVALID_HANDLE`** (the accumulator is mid-group, not a sealed committable state). **Steady-state thunk** → abort on exception escape (`[2i §5.2]`).

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
/* Begin a NESTED group WITHIN an entry (FR-012 MUST — ABI added per user
   decision 2026-06-24). Closed by the SAME fixpp_msg_group_end under a LIFO
   close-order contract. */
fixpp_error_t fixpp_entry_group_begin(fixpp_entry_t* e, uint16_t group_tag, fixpp_group_builder_t** b_out);
fixpp_error_t fixpp_msg_group_end    (fixpp_msg_t* msg, fixpp_group_builder_t* b);
```

- `group_begin` / `entry_group_begin`: `TYPE_MISMATCH` if `group_tag` not a dictionary group; `INVALID_HANDLE` on inbound msg. `entry_set_*` rejects framing tags → `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` (same INV-3 rule as `msg_set_*`).
- `group_end`: seals; **invalidates** the builder + its entry handles (reuse → `INVALID_HANDLE`). Reused for nested close.
- **`fixpp_entry_set_bytes` is intentionally omitted** (analyze C3): entry setters cover the four dictionary-typed flavours (string/int/double/decimal); the type-agnostic raw-bytes setter exists only at the msg level (`fixpp_msg_set_bytes`) as the escape for custom/extension top-level tags. This keeps the group-build surface at exactly 8 symbols (`msg_group_begin` + `group_builder_add_entry` + 4 `entry_set_*` + `entry_group_begin` + `msg_group_end`). If a raw-bytes entry field is later needed it is an additive `+1` symbol (golden → 34), not a v1.0 surface.
- **Nested LIFO contract:** an entry opens a nested group via `fixpp_entry_group_begin`; builders MUST be ended in reverse of begin order. Ending a parent builder while a younger (nested) builder it parents is still open → **`FIXPP_ERR_INVALID_HANDLE`** (out-of-order close). One net-new exported symbol (`fixpp_entry_group_begin`) vs the pre-#6 count; close reuses `fixpp_msg_group_end` (the cleaner shape — no separate nested-close symbol).
