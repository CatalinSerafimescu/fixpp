# Contract — Message read surface (CA-008 + CA-010-read)

Header: `include/fix/c_api/message.h` (NEW). Impl: `src/capi/message_read.cpp`. All thunk into `wire::MessageView<Index>` (`parser.hpp`); zero global-heap; no exception crosses `extern "C"` (steady-state → abort on escape). **Reentrancy:** the shared `fixpp_msg_get_*` accessors carry the **single conservative class `FIXPP_REQUIRES_SESSION_LOCK`** (matching the inherited `[2i §4.6]` annotation — NOT edited here); `fixpp_msg_version` `FIXPP_THREAD_SAFE`. The detached-clone-read `FIXPP_THREAD_SAFE` property is a **documented runtime/handle-state guarantee, OUTSIDE the per-symbol gate** (the static gate checks one annotation per declaration and cannot distinguish a clone read from an inbound-flyweight read on the same symbol) — a clone owns its arena, its reads are callable from any thread, and the caller serialises concurrent same-handle access. The seam-#13 cross-strand contract is this documented/runtime guarantee, not a gate-enforced one.

**Framed toApp view (New-P2-b / FR-024).** The same `fixpp_msg_t` opaque type has two read behaviours: as an outbound *accumulator* the framing tags 8/9/34/49/52/56/10 are forbidden (INV-3), but the `fixpp_msg_t` exposed inside the **toApp callback window** is a read-only **FRAMED** view parsed from the complete built frame — so 8/9/34/49/52/56/10 (`8=`/`9=`/`34=`/`49=`/`52=`/`56=`/`10=`) **ARE** readable at their wire positions. A consumer reading `34=`/`49=` in toApp is reading the session-stamped frame, by design.

## CA-008 — field read accessors

```c
/* String/bytes: *value_out ALIASES the wire buffer (no copy, no free); valid
   until the next set_* on this msg, the inbound dispatch window close, or
   fixpp_msg_destroy. */
fixpp_error_t fixpp_msg_get_string (const fixpp_msg_t* msg, uint16_t tag,
                                    const char** value_out, size_t* len_out);
fixpp_error_t fixpp_msg_get_bytes  (const fixpp_msg_t* msg, uint16_t tag,
                                    const uint8_t** bytes_out, size_t* len_out);
fixpp_error_t fixpp_msg_get_int    (const fixpp_msg_t* msg, uint16_t tag, int64_t* value_out);
fixpp_error_t fixpp_msg_get_double (const fixpp_msg_t* msg, uint16_t tag, double*  value_out);
fixpp_error_t fixpp_msg_get_decimal(const fixpp_msg_t* msg, uint16_t tag, fixpp_decimal_t* value_out);
fixpp_error_t fixpp_msg_has_tag    (const fixpp_msg_t* msg, uint16_t tag, bool* present_out);
fixpp_error_t fixpp_msg_version    (const fixpp_msg_t* msg, fixpp_resolved_msg_version_t* version_out);
fixpp_error_t fixpp_msg_get_msg_type(const fixpp_msg_t* msg, const char** value_out, size_t* len_out);
```

**Return codes:** `OK`; `NULL_HANDLE` (NULL msg/out); `INVALID_HANDLE` (destroyed/tombstoned/wrong-tag); `TAG_NOT_FOUND` (absent); `TYPE_MISMATCH` (dictionary-known wrong flavour — only when the view carries a `classify_fn`, D-5); `WIRE_INVALID_FRAME` (int/double non-numeric bytes); `DECIMAL_INVALID`/`DECIMAL_PRECISION_LOSS` (decimal). **No `BUFFER_TOO_SMALL`** — the read path is purely aliasing (no caller buffer).

Backing: `get(tag) → expected_t<field_view>` (`parser.hpp:200`); `get_decimal(tag, mr)` (`parser.hpp:215`, `mr` = a scratch resource); `msg_type()` (`parser.hpp:143`). `has_tag` = `get(tag).has_value()`. `int`/`double` parse `field_view::as_string()` ASCII → number (parse failure → `WIRE_INVALID_FRAME`).

## CA-010 — repeating-group read

```c
typedef struct fixpp_group fixpp_group_t;

fixpp_error_t fixpp_msg_get_group(const fixpp_msg_t* msg, uint16_t group_tag,
                                  const fixpp_group_t** group_out, size_t* count_out);
fixpp_error_t fixpp_group_get_field_string (const fixpp_group_t* g, size_t i, uint16_t tag,
                                            const char** v_out, size_t* len_out);
fixpp_error_t fixpp_group_get_field_int    (const fixpp_group_t* g, size_t i, uint16_t tag, int64_t* v_out);
fixpp_error_t fixpp_group_get_field_double (const fixpp_group_t* g, size_t i, uint16_t tag, double* v_out);
fixpp_error_t fixpp_group_get_field_decimal(const fixpp_group_t* g, size_t i, uint16_t tag, fixpp_decimal_t* v_out);
fixpp_error_t fixpp_group_get_nested_group (const fixpp_group_t* g, size_t i, uint16_t nested_tag,
                                            const fixpp_group_t** nested_out, size_t* nested_count_out);
```

Backing: `msg.view->offsets().group_slices(group_tag)` (D-4) → instance slices + count; per-entry field read walks instance `[i]`; nested via the instance sub-walk. Cursor aliases the parent msg; lifetime bounded by it. Codes per E-2.
