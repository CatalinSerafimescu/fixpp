# Phase 1 Data Model — 051 C-ABI Feature C

Entities are C-ABI handle shapes + their internal C++ backing (`src/capi/capi_internal.hpp`). All opaque to the consumer; defined engine-internal per `[2i §4.2]`.

## E-1 — `fixpp_msg_t` (dual-flavour message handle)

| Field | Inbound flavour | Outbound flavour |
|---|---|---|
| Backing | `const wire::MessageView<Index>*` (borrowed; stack, `capi_internal.hpp:114`) | a mutable in-arena **accumulator** (E-3), arena = `Session::session_arena()` |
| Mutability | **immutable** — `set_*`/group-build → `FIXPP_ERR_INVALID_HANDLE` (`[2i §10] Q5`) | mutable until commit/destroy |
| Lifetime | receive-callback dispatch window | until `fixpp_msg_destroy` **or** session-close tombstone (E-9) |
| Type-tag | 049/050 handle tag; `FIXPP_HANDLE_TAG_DEAD` flipped only on the handle's own `fixpp_msg_destroy` | same. **Session-close tombstone is a LAZY token check, NOT a tag flip** (E-9): nothing enumerates live outbound handles at session close, so the dead-tag flip cannot be the mechanism. |
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
- **INV-3** — **no framing tag** (`8/9/34/49/52/56/10`) may be set; `set_*` (and `fixpp_entry_set_*`) reject at set-time → **`FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN`** (1405), fail-fast. This is a distinct **message-construction** reject code, NOT the session-domain `SESSION_INVALID_ARGUMENT` (semantically wrong for "client set a framing tag on an outbound accumulator" — see error-block-amendment talking point 2).
- **INV-4** — groups emit in dictionary-grammar order: `NoXXX=count\x01` then each instance delimiter-first; `count` == number of `add_entry` calls; an unfinished builder (no `group_end`) is a commit-time error.
- **INV-5** — all bytes live in the session arena (no global heap); the committed span aliases it (E-1/D-3); invalidated by the next mutation or destroy.
- **INV-6** — committed body over the frame cap (~3800 B per `session.cpp:4021`) → `FIXPP_ERR_WIRE_LIMIT_EXCEEDED`.

**Set/overwrite semantics:** `set_*` on an existing tag overwrites in place (ordered structure, not append-only); `remove_tag` removes (idempotent). Re-commit allowed; the prior committed span is invalidated by any intervening mutation.

## E-4 — `fixpp_group_builder_t` / `fixpp_entry_t` (outbound group construction)

- `fixpp_msg_group_begin(msg, group_tag, &builder)` → opens a group in the accumulator (E-3); rejects a non-group `group_tag` → `FIXPP_ERR_TYPE_MISMATCH`, and an inbound msg → `FIXPP_ERR_INVALID_HANDLE`.
- `fixpp_group_builder_add_entry(builder, &entry)` → appends an instance; returns a writable entry handle.
- `fixpp_entry_set_{string,int,double,decimal}(entry, tag, …)` → sets a field on the current instance (same INV-2/INV-3 rules).
- `fixpp_msg_group_end(msg, builder)` → seals the group, **invalidates** the builder + all its entry handles (`FIXPP_ERR_INVALID_HANDLE` on reuse).
- **Nested groups (FR-012 MUST — ABI added per user decision 2026-06-24).** An entry begins a nested group via a dedicated entry-scoped opener: `fixpp_entry_group_begin(fixpp_entry_t* entry, uint16_t group_tag, fixpp_group_builder_t** b_out)` → begins a repeating group WITHIN that entry, returning a nested builder. The nested group is closed with the **same** `fixpp_msg_group_end(msg, builder)` (reused, not a separate nested-close symbol — the cleaner shape), under a documented **LIFO close-order contract**: builders MUST be ended in reverse of begin order. Ending a builder while a younger (nested) builder it parents is still open → `FIXPP_ERR_INVALID_HANDLE` (out-of-order close). `fixpp_entry_group_begin` rejects a non-group `group_tag` → `FIXPP_ERR_TYPE_MISMATCH`. This is the ONE net-new symbol vs the pre-#6 plan (see plan §Scale/Scope exact count).

## E-5 — `fixpp_error_t` session/app + message-construction block (the `[2i §4.3]` amendment, D-6/D-7)

Placed in a **NEW dedicated Phase-4-owned block `[1400,1499]`** (RULED at Gate A round 1 — NOT the cross-cutting `[11,99]` sentinel range; a dedicated domain avoids permanently relabelling the `[0,99]` boundary-sentinel block). `[1400,1499]` is the last 100-wide block in the budget (`[2i §1.1]`).

| C-ABI code (`[1400,1499]`) | numeric | C++ ordinal mapped | remediation class |
|---|---|---|---|
| `FIXPP_ERR_SESSION_INVALID_ARGUMENT` | 1400 | `session_invalid_argument` (119) | bad argument |
| `FIXPP_ERR_SESSION_INVALID_STATE` | 1401 | `session_invalid_state_for_send` (77) | bad state-for-send |
| `FIXPP_ERR_APP_DO_NOT_SEND` | 1402 | `app_do_not_send` (129) | business veto |
| `FIXPP_ERR_APP_CALLBACK_THREW` | 1403 | `app_callback_threw` (130) | callback failure |
| `FIXPP_ERR_APP_PAYLOAD_MALFORMED` | 1404 | `app_payload_malformed` (131) | malformed payload |
| `FIXPP_ERR_MSG_FRAMING_TAG_FORBIDDEN` | 1405 | **(none — C-ABI construction reject)** | message construction |

The 6 codes are pinned (not "proposed") — `contracts/error-block-amendment.md` carries the authoritative `#define` block. Introducing-minor = 4 for all six. Each gets a `fixpp_strerror` string + an `error_codes_v1.txt` row. `translate()` re-points the **five** mapped arms off `FIXPP_ERR_UNKNOWN`; **1405 has no `translate()` arm** (no C++ ordinal — raised only by the `set_*`/`entry_set_*` framing-tag reject path, INV-3). Downgrade is keyed on a **per-code** introducing-minor lookup (D-7), so the six new minor-4 codes downgrade for a sub-4 consumer while every existing minor-2/3 code survives.

## E-6 — Send-callback slot (toApp trampoline, D-8)

`SessionSlot` (`capi_internal.hpp:51`) gains: `fixpp_send_cb send_cb = nullptr; void* send_userdata = nullptr;`. `CapiApplication::toApp` reads it on the originate path; absent → returns `{}` (send, the default Application behaviour). Verdict mapping per FR-023 / `contracts/toapp-callback.md`.

## E-7 — Reentrancy taxonomy (per-symbol, D-10)

See plan §X.5 + D-10. The shared `fixpp_msg_get_*` read accessors carry the **single conservative class `FIXPP_REQUIRES_SESSION_LOCK`** (matching the inherited `[2i §4.6]` annotation — NOT edited by this feature). The per-symbol static reentrancy gate (`check_capi_reentrancy.sh`) checks exactly one annotation per declaration; it has **no runtime handle-flavour dimension** and therefore **cannot** distinguish an inbound-flyweight read from a detached-clone read on the same symbol. The clone-read `FIXPP_THREAD_SAFE` property is therefore a **documented runtime/handle-state guarantee** (a clone owns its arena, callable from any thread; caller serialises same-handle access), recorded in `message-read.md` + the seam-#13 contract — **outside** the per-symbol gate, not enforced by it. Zero new symbols; the gate is unchanged.

## E-8 — Version

`FIXPP_C_ABI_VERSION_MINOR` 3 → 4 (`version.h`). `FIXPP_C_ABI_VERSION` recomputed.

## E-9 — Tombstone state machine (outbound, FR-009a / D-9) — single LAZY mechanism

`outbound fixpp_msg_t` states: **live** → (`fixpp_msg_destroy`) → **destroyed**; **live** → (owning session close/destroy) → **tombstoned**. In destroyed/tombstoned: `set_*`/`commit`/group-build → `FIXPP_ERR_INVALID_HANDLE`; `fixpp_msg_destroy` → no-op `FIXPP_ERR_OK`. A clone is exempt (own session-independent arena, FR-009).

**Two distinct detections — ONE coherent model (reconciled to D-9; the prior eager tag-flip wording is removed):**

1. **Own-`destroy`** flips the handle's `tag_` to `FIXPP_HANDLE_TAG_DEAD` (the 049/050 per-handle mechanism). Eager, on its own struct.
2. **Session-close tombstone is LAZY — a validity-token check, NOT a tag flip.** No registry enumerates live outbound handles at session-close time (verified absent in `capi_internal.hpp`: `SessionSlot` = `{cb,userdata,established}`; `fixpp_session` = `{engine,id,slot,valid}`; no list of child `fixpp_msg`, no session generation counter), so nothing *can* eager-flip every handle. Instead the outbound `fixpp_msg` holds a **self-contained validity token that outlives the session arena**: a `std::weak_ptr<SessionLiveness>` aimed at a per-session liveness control block. The strong `shared_ptr<SessionLiveness>` is owned by the **engine-retained `fixpp_session` shell** (which outlives `Session::session_arena()`). The token lives **outside** the reclaimed arena.

**Expiry MUST cover EVERY arena-reclamation path (not just `close()`).** Source-verified ordering: `fixpp_session_close` calls `Session::close(graceful)` but does **NOT** destroy the C++ `Session` — the `Session` and its `session_arena_` are reclaimed only at `fixpp_engine_destroy` (`state_.reset()` destroys `EngineState::engine_` → its Sessions), and the `fixpp_session` shells are retained even past engine-destroy (`sessions_`). So a `fixpp_msg` created and then orphaned by `fixpp_engine_destroy` **without** a prior `fixpp_session_close` would reclaim the arena while a close-only token still `.lock()`s — the SAME 050 UAF on a different path. Therefore the strong `SessionLiveness` ref MUST be reset on **all** teardown paths: `fixpp_session_close`, AND `fixpp_engine_destroy` (which already iterates `sessions_` — reset each session's strong ref before/as `state_.reset()` reclaims the arena), AND any internal session removal. **Invariant: token expiry happens-before arena teardown on every path.**

**Check-before-deref (the UAF-closing invariant):** every `set_*` / `fixpp_entry_set_*` / `commit` / group-build / `fixpp_msg_destroy` MUST first (a) check `tag_ != FIXPP_HANDLE_TAG_DEAD` and (b) `weak.lock() != nullptr`. If either fails → `FIXPP_ERR_INVALID_HANDLE` (destroy → no-op `OK`) returned **before any arena dereference**. Because the weak token lives outside the arena and its expiry happens-before arena teardown on every path, the lazy check is always safe even after the arena is genuinely freed — closing the exact 050 arena-UAF class (`[[feedback_cabi_handle_destroy_needs_tombstone]]`).
