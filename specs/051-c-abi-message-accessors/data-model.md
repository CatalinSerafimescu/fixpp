# Phase 1 Data Model — 051 C-ABI Feature C

Entities are C-ABI handle shapes + their internal C++ backing (`src/capi/capi_internal.hpp`). All opaque to the consumer; defined engine-internal per `[2i §4.2]`.

## E-1 — `fixpp_msg_t` (dual-flavour message handle)

| Field | Inbound flavour | Outbound flavour |
|---|---|---|
| Backing | `const wire::MessageView<Index>*` (borrowed; stack, `capi_internal.hpp:114`) | a mutable in-arena **accumulator** (E-3), arena = `Session::session_arena()` |
| Mutability | **immutable** — `set_*`/group-build → `FIXPP_ERR_INVALID_HANDLE` (`[2i §10] Q5`) | mutable until commit/destroy |
| Lifetime | receive-callback dispatch window | until `fixpp_msg_destroy` **or** session-close tombstone (E-9) |
| Type-tag | 049/050 handle tag; `FIXPP_HANDLE_TAG_DEAD` on destroy/tombstone | same |
| Reentrancy of reads | `FIXPP_REQUIRES_SESSION_LOCK` | n/a (outbound is built then committed) |

A **clone** (`fixpp_msg_clone`) is an outbound-flavour handle with its **own** arena (session-independent), reads `FIXPP_THREAD_SAFE`, **not** tombstoned by session close.

**Validation/state:** NULL → `FIXPP_ERR_NULL_HANDLE`; wrong type-tag / destroyed / tombstoned → `FIXPP_ERR_INVALID_HANDLE`.

## E-2 — `fixpp_group_t` (read-only repeating-group cursor)

- Backing: the `OffsetTable::group_slices(NoTag)` span (per-instance arena slices) + the parent view + token (D-4).
- Fields exposed: entry `count`; per-entry field read by `(entry_index, tag)`; nested descent.
- Lifetime: bounded by the parent `fixpp_msg_t`. Aliases the message; no copy.
- Errors: absent group → `FIXPP_ERR_TAG_NOT_FOUND`; non-group tag → `FIXPP_ERR_TYPE_MISMATCH`; `entry_index ≥ count` → `FIXPP_ERR_INDEX_OUT_OF_RANGE`; absent field in entry → `FIXPP_ERR_TAG_NOT_FOUND`; flavour mismatch → `FIXPP_ERR_TYPE_MISMATCH`.

## E-3 — Outbound message accumulator (the net-new core, D-2)

An ordered, mutable, arena-resident structure: `msg_type` (the `35=` value) + an **ordered list of entries**, where each entry is either a scalar `(tag, value_bytes)` or a **group** `(no_tag, [instance…])`, each instance an ordered list of `(tag, value_bytes)` (recursively, for nested groups).

**Invariants (the emission contract — pinned because `send_impl` splices, D-2):**
- **INV-1** — `35=<type>` is emitted first; `msg_type` is non-empty.
- **INV-2** — every scalar field emits `digit-tag=non-empty-value\x01`; empty value → `set_*` rejects (the session would reject `app_payload_malformed`).
- **INV-3** — **no framing tag** (`8/9/34/49/52/56/10`) may be set; `set_*` rejects at set-time → `FIXPP_ERR_INVALID_ARGUMENT`-class (`FIXPP_ERR_SESSION_INVALID_ARGUMENT` or a dedicated reject), fail-fast.
- **INV-4** — groups emit in dictionary-grammar order: `NoXXX=count\x01` then each instance delimiter-first; `count` == number of `add_entry` calls; an unfinished builder (no `group_end`) is a commit-time error.
- **INV-5** — all bytes live in the session arena (no global heap); the committed span aliases it (E-1/D-3); invalidated by the next mutation or destroy.
- **INV-6** — committed body over the frame cap (~3800 B per `session.cpp:4021`) → `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`.

**Set/overwrite semantics:** `set_*` on an existing tag overwrites in place (ordered structure, not append-only); `remove_tag` removes (idempotent). Re-commit allowed; the prior committed span is invalidated by any intervening mutation.

## E-4 — `fixpp_group_builder_t` / `fixpp_entry_t` (outbound group construction)

- `fixpp_msg_group_begin(msg, group_tag, &builder)` → opens a group in the accumulator (E-3); rejects a non-group `group_tag` → `FIXPP_ERR_TYPE_MISMATCH`, and an inbound msg → `FIXPP_ERR_INVALID_HANDLE`.
- `fixpp_group_builder_add_entry(builder, &entry)` → appends an instance; returns a writable entry handle.
- `fixpp_entry_set_{string,int,double,decimal}(entry, tag, …)` → sets a field on the current instance (same INV-2/INV-3 rules).
- `fixpp_msg_group_end(msg, builder)` → seals the group, **invalidates** the builder + all its entry handles (`FIXPP_ERR_INVALID_HANDLE` on reuse).
- Nested groups: an entry may itself `group_begin` a nested group (LIFO close order).

## E-5 — `fixpp_error_t` session/app block (the `[2i §4.3]` amendment, D-6/D-7)

| C-ABI code (cross-cutting `[11,99]`) | numeric | C++ ordinal mapped | remediation class |
|---|---|---|---|
| `FIXPP_ERR_SESSION_INVALID_ARGUMENT` | 11 | `session_invalid_argument` (119) | bad argument |
| `FIXPP_ERR_SESSION_INVALID_STATE` | 12 | `session_invalid_state_for_send` (77) | bad state-for-send |
| `FIXPP_ERR_APP_DO_NOT_SEND` | 13 | `app_do_not_send` (129) | business veto |
| `FIXPP_ERR_APP_CALLBACK_THREW` | 14 | `app_callback_threw` (130) | callback failure |
| `FIXPP_ERR_APP_PAYLOAD_MALFORMED` | 15 | `app_payload_malformed` (131) | malformed payload |

(Exact symbol names/numbers finalised in `contracts/error-block-amendment.md`; numbers shown are the proposed next-free slots after `FIXPP_ERR_CAPI_CONFIG_INVALID=10`.) Introducing-minor = 4. Each gets a `fixpp_strerror` string + an `error_codes_v1.txt` row. `translate()` re-points each off `FIXPP_ERR_UNKNOWN`.

## E-6 — Send-callback slot (toApp trampoline, D-8)

`SessionSlot` (`capi_internal.hpp:51`) gains: `fixpp_send_cb send_cb = nullptr; void* send_userdata = nullptr;`. `CapiApplication::toApp` reads it on the originate path; absent → returns `{}` (send, the default Application behaviour). Verdict mapping per D-8.

## E-7 — Reentrancy taxonomy (per-symbol, D-10)

See plan §X.5 + D-10. The CI gate distinguishes inbound-flyweight-read (`REQUIRES_SESSION_LOCK`) from detached-clone-read (`THREAD_SAFE`).

## E-8 — Version

`FIXPP_C_ABI_VERSION_MINOR` 3 → 4 (`version.h`). `FIXPP_C_ABI_VERSION` recomputed.

## E-9 — Tombstone state machine (outbound, FR-009a / D-9)

`outbound fixpp_msg_t` states: **live** → (`fixpp_msg_destroy`) → **destroyed**; **live** → (owning session close/destroy) → **tombstoned**. In destroyed/tombstoned: `set_*`/`commit`/group-build → `FIXPP_ERR_INVALID_HANDLE`; `fixpp_msg_destroy` → no-op `FIXPP_ERR_OK`. Detection via the handle type-tag flip to `FIXPP_HANDLE_TAG_DEAD` (049/050 mechanism). A clone is exempt (own arena).
