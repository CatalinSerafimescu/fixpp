# Contract — `[2i §4.3]` session/app error-block amendment (Article XX)

This is the one contract that **reopens the signed-off `[2i]`** (CHK030). The review is **folded into this feature's Gate A** per Article XX (043 precedent). The artifact Gate A reviews is the actual `[2i §4.3]` diff + the co-update set below.

## The new block (cross-cutting `[11,99]`, D-6)

Add to `include/fix/c_api/error.h`, in the cross-cutting block, at the next free slots after `FIXPP_ERR_CAPI_CONFIG_INVALID = 10`:

```c
#define FIXPP_ERR_SESSION_INVALID_ARGUMENT  ((fixpp_error_t) 11)  /* core::error session_invalid_argument (119) */
#define FIXPP_ERR_SESSION_INVALID_STATE     ((fixpp_error_t) 12)  /* core::error session_invalid_state_for_send (77) */
#define FIXPP_ERR_APP_DO_NOT_SEND           ((fixpp_error_t) 13)  /* core::error app_do_not_send (129) */
#define FIXPP_ERR_APP_CALLBACK_THREW        ((fixpp_error_t) 14)  /* core::error app_callback_threw (130) */
#define FIXPP_ERR_APP_PAYLOAD_MALFORMED     ((fixpp_error_t) 15)  /* core::error app_payload_malformed (131) */
/* [16, 99] remain reserved for cross-cutting growth. */
```

(Numeric slots 11–15 are the proposal; final numbers ratified at Gate A on the diff. The `119/77/129/130/131` in comments are the **C++ ordinals**, NOT the C-ABI codes.)

## Co-update set (one pass, or one site lags — FR-015/FR-016)

| Site | Change |
|---|---|
| `include/fix/c_api/error.h` | +5 `#define`s above |
| `src/capi/error.cpp` `translate()` | re-point the 5 arms (currently `→ FIXPP_ERR_UNKNOWN`) to the new codes; the no-`default` switch stays exhaustive |
| `src/capi/error.cpp` `k_strerror_table` | +5 static English strings (zero-alloc) |
| `src/capi/error.cpp` `kIntroducingMinor` | the 5 new codes → introducing-minor **4** (the 0.4.0 minor) → live downgrade `consumer_minor<4 → UNKNOWN` |
| `tools/abi_history/error_codes_v1.txt` | append 5 rows (numeric ↔ symbol ↔ introducing doc-rev) |
| `tools/check_capi_occupancy.sh` | cross-cutting `[0,99]` count 11 → 16 (no new ownership term — the block is 2i-owned) |
| `.specify/2i-capi.md` | §1.1 magnitude-domain table (source of truth) + §1.1 final-layout block + §3.11 prose + §4.3 inline comments + §6.5 prior-doc total + Appendix D.2 — swept in ONE pass so the occupancy gate stays green |
| `.specify/2i-capi.md` §4.7 | note the stale `fixpp_session_send(session, msg)` prose is reconciled to the byte-span model (FR-008a) |

## Gate A talking points (pre-stage so the review doesn't raise them cold)

1. **Why `[11,99]` not a new block** — D-6: C-ABI-boundary errors, lowest gate disruption, no new ownership term, beside `NULL_HANDLE`/`INVALID_HANDLE`. Alternative `[1400,1499]` considered (cleaner domains, but last-in-budget + Phase-4-block-in-Phase-2-layout + new gate term).
2. **Why 5 distinct codes vs §4.3 "coalesce"** — 5 distinct consumer-remediation classes (bad arg / bad state / veto / threw / malformed), not per-variant noise — the same test §4.3 applies to its per-doc groups.
3. **Reachability** — all 5 are end-to-end-witnessable from pure C now that the toApp hook ships (D-13); SC-004 asserts each live.
4. **Closes** — L-050-4 (block published) + L-049-2 (session/app arms; log/otel stay deferred-by-design, `[1000,1099]` reserved) + the 050 FR-015/SC-005.
