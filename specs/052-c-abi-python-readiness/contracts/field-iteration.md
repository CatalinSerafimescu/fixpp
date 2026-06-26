# Contract: inbound field iteration (GAP-003 / FR-006..007)

Added to `include/fix/c_api/message.h`. Net-new additive (not in `[2i]`, not a non-goal). Wraps any
view-backed handle's existing `OffsetTable` (D-1) — inbound parsed messages **and** 051 view-backed
clones — no new parsing, zero global-heap.

```c
/** A single field view aliasing the wire buffer. NOT NUL-terminated. */
typedef struct fixpp_msg_field {
    uint16_t        tag;
    const uint8_t  *value;   /* aliases the wire buffer; valid for the msg lifetime */
    size_t          len;
} fixpp_msg_field_t;

/**
 * fixpp_msg_field_count — number of fields present in the parsed message.
 * Reentrancy: REQUIRES_SESSION_LOCK (inbound read).
 * @return FIXPP_ERR_OK + *count_out; FIXPP_ERR_NULL_HANDLE; FIXPP_ERR_INVALID_HANDLE (destroyed/expired).
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_field_count(const fixpp_msg_t* msg, size_t* count_out);

/**
 * fixpp_msg_field_at — the field at wire-order index `index`.
 *
 * Enumerates EVERY OffsetTable entry of the parsed message in wire/document order — one entry per wire
 * occurrence (a tag repeated across N repeating-group instances yields N entries; this is the multiset,
 * NOT a bi-directional "== the typed-getter-readable set"). Session header/trailer framing tags
 * 8/9/35/49/56/34/52/10 are included when the view holds them; no app-body filter. The scalar typed
 * getters read a first-occurrence subset (FR-007). field_out->value aliases the wire buffer (no copy),
 * valid for the parent handle's OWN lifetime (the dispatch window for an inbound message; until
 * fixpp_msg_destroy for a 051 view-backed clone).
 * Reentrancy: REQUIRES_SESSION_LOCK.
 * @return FIXPP_ERR_OK + *field_out; FIXPP_ERR_INDEX_OUT_OF_RANGE (index >= count);
 *         FIXPP_ERR_NULL_HANDLE; FIXPP_ERR_INVALID_HANDLE.
 */
FIXPP_API_EXPORT fixpp_error_t fixpp_msg_field_at(
    const fixpp_msg_t* msg, size_t index, fixpp_msg_field_t* field_out);
```

**Implementation notes (src/capi/message_read.cpp):**
- Resolve the view-backed `fixpp_msg`'s parsed view (inbound or 051 clone — both hold a
  `MessageView<Index>`), take its `OffsetTable` (`offsets()` / `entries()`). `field_count` →
  `entries().size()`. `field_at(i)` → entry `e = entries()[i]`; `field_out = { e.tag, wire_base + e.offset,
  e.length }`. (`offset_table.hpp:85-87`.)
- **D-1a — RESOLVED at Gate A (not an open implement-time question):** the inbound (and clone) `fixpp_msg`
  view member is `const MessageView<access_mode::Index>*` (`capi_internal.hpp:227`; clones back it with an
  `owned_view_` `MessageView<Index>`, `:262-263`), so `offsets()`/`entries()` are available. The Scan-mode
  fallback is dead; no per-message-arena index build is needed.
- Steady-state thunks — exception escape → fatal log + `std::abort` (FR-011), NOT translated.
- Type-tag/tombstone check first (a `fixpp_session_t*` passed as `fixpp_msg_t*` → `INVALID_HANDLE`).
- **Clone-handle reentrancy (carry-forward of 051 FR-018):** the static annotation on both symbols stays
  `FIXPP_REQUIRES_SESSION_LOCK` (matching `[2i §4.6]`), but on a **detached 051 clone handle** (owns its
  own arena, survives session/engine teardown) the reads are a **documented runtime handle-state
  `THREAD_SAFE` guarantee OUTSIDE the per-symbol gate** — callable off the session strand from any thread,
  caller serializes concurrent same-handle access — mirroring the shipped `fixpp_msg_get_*` family
  (`message.h:109-112`). NOT a second annotation; the `check_capi_reentrancy.sh` gate is unchanged.

**Witness (US3 / SC-002 / SC-003):** `tests/capi/message_field_iteration_test.cpp` — inside a receive
callback, `field_count` then `field_at` over `[0,count)`; assert the **one-directional FR-007 invariant**
(every scalar-getter-readable tag appears in the enumeration with a byte-matching value for its **first**
occurrence; the enumeration is a superset under repeated tags). **The witness MUST carry a repeating-group
message** (≥1 repeated tag) — a flat message masks the superset/first-occurrence relationship exactly as
the 051 STRING-only test dict masked the group-grammar gap. `index==count` → `INDEX_OUT_OF_RANGE`; the
path is **zero global-heap** under the mallocnesia + counting-resource dual gate and each `value` aliases
the wire buffer. (Iteration also works on a 051 clone handle, with the clone's destroy-bounded lifetime.)
A **clone-iteration cross-strand witness** detaches a 051 clone and iterates it **off the session strand**
(a different thread, after the source dispatch window closed), asserting `field_count`/`field_at` read
correctly — exercising the runtime-`THREAD_SAFE` clone guarantee (FR-008 / 051 FR-018).
