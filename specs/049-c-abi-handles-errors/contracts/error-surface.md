# Contract — Error surface (CA-002)

**Header**: `include/fix/c_api/error.h` (NEW). **Impl**: `src/capi/error.cpp` (NEW).

## Published surface (C-clean)
- `typedef int32_t fixpp_error_t;` — underlying type frozen as `int32_t`; codes are `#define` constants, never a C `enum` (size stability).
- The full per-domain numeric layout from `[2i §4.3]` (see data-model E-2). Reserved blocks left as comments.
- `FIXPP_API_EXPORT const char* fixpp_strerror(fixpp_error_t code);`

## `fixpp_strerror` contract (`[2i §4.4]`)
- Returns a **non-null**, static-storage `const char*` for every published code; caller MUST NOT free.
- **Zero allocation.** Static `const char* const` table.
- Out-of-range / undefined code → the documented "unknown error" string (no crash, no alloc).
- Reentrancy: **thread-safe**.

## Engine-internal (NOT exported — keeps §X.2)
- `fixpp_error_t fixpp_capi::detail::translate(fixpp::core::error) noexcept;` — one `switch`, no `default`, over the **116 enumerators** (highest assigned slot 131, holes {2–9, 14–19, 70}) → audited coalesced codes (data-model E-3; not a mechanical transcription — `session_*`/`log_*`/`otel_*`/`app_*`/`out_of_memory` are override groups). `-Wswitch` guards totality (not correctness — see the test oracle below).
- `fixpp_error_t fixpp_capi::detail::translate_for_consumer(fixpp_error_t, uint16_t consumer_minor) noexcept;` — forward-compat downgrade (data-model E-5 / research D-3).

## `translate()` totality (research D-8 / data-model E-3)
- The `switch` is **total** (no `default`; `-Wswitch`). The four groups `[2i §4.3]` does not publish — `session_*`, `log_*`, `otel_*`, `app_*` (and `out_of_memory`) — map to `FIXPP_ERR_UNKNOWN (2)` **as documented v1.0 behaviour**, each with a deferral comment (`session_*` → "publish FIXPP_ERR_SESSION_* in Feature B", L-049-2). No `[2i]` amendment in Feature A.

## Acceptance (maps to SC-002/SC-004/SC-006)
- Drive the **exact 116-enumerator set** (from a checked-in expected table or the `error_message()` arms — never a 1..131 range, which drives 15 non-existent values) through `translate()` and assert each maps to its **specific expected C code** from the checked-in (variant → exact code) table (the same artifact seeding `error_codes_v1.txt`); **mutation-tested** (flip one arm → RED). "Returns *a* published code" is a bypassable proxy and is not sufficient. On top of the oracle, the `session_*`/`log_*`/`otel_*`/`app_*`/`out_of_memory` override arms are asserted `== FIXPP_ERR_UNKNOWN` **explicitly** (so a Feature-B refinement trips the test).
- Enumerate all published codes → `fixpp_strerror` non-null + non-empty; an undefined value → "unknown error".
- `translate_for_consumer(future_code, old_minor)` → `UNKNOWN`; `(in_range_code, current_minor)` → unchanged.
- `check_capi_occupancy.sh` passes; a deliberately re-defined slot makes it fail.
