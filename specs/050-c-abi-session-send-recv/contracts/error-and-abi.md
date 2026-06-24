# Contract — Error block + downgrade go-live + ABI gates

## FIXPP_ERR_SESSION_* block — DEFERRED (L-050-4; L-049-2 stays open)

- **No session/app error block is published by Feature B.** `[2i §4.3]` has no published session/app block (`session_*`/`app_*` are the unpublished `FIXPP_ERR_UNKNOWN` bucket, `error.h:51`; coalesced remediation-class design; the occupancy gate's `EXPECTED` map is hardcoded to the `[2i §4.3]` master). Publishing per-variant codes would require editing the signed-off `[2i]` (forbidden by the Gate-A LEAVE / CHK030). **User decision 2026-06-24: DESCOPE.**
- **No `error.h` block, no `translate()` re-point, no `error_codes_v1.txt` append, no occupancy-gate delta** — the occupancy gate stays GREEN unchanged. The 5 reachable session/app arms (119/77/129/130/131) **remain `FIXPP_ERR_UNKNOWN`**; **L-049-2 stays open**. Publishing the block is a dedicated future `[2i §4.3]` amendment.
- The mutation oracle (`error_block_test`) asserts these 5 arms map to `FIXPP_ERR_UNKNOWN` (the documented deferred set) and that the existing-published send arms (`wire_frame_too_large`/`store_seqnum_overflow`/`session_already_closed`/cancellation) are NOT silently re-pointed.

## Downgrade go-live (discharges L-049-1) — UNCHANGED by the descope

- **No real code ships at introducing_minor 3** (the session/app block is deferred — L-050-4); `error_codes_v1.txt` gets no append and the occupancy gate (`check_capi_occupancy.sh`) stays GREEN unchanged.
- The downgrade **mechanism still goes live**: `fixpp_engine_create` records `consumer_minor`; every fallible C-ABI return runs `translate_for_consumer(code, consumer_minor)` (Feature A's pure fn). `consumer_major != 0` → `FIXPP_ERR_VERSION_MISMATCH` at create.
- **SC-004 is witnessed with a SYNTHETIC minor-3 code** (vs a minor-2 consumer): the test injects a synthetic code born at minor 3, a `consumer_minor < 3` downgrades it → `FIXPP_ERR_UNKNOWN`, a `consumer_minor >= 3` sees the real code. No real new code is needed — the mechanism ships and is provable synthetically (research D-9/D-11).

## ABI gates (FR-018)

- **nm symbol-golden** (`tests/abi/golden/fixpp_capi_symbols.txt`): append every new exported symbol (engine create/start/destroy; session open/close/send/register_callback/is_established; the two config-builder families). The per-PR nm gate verifies the exported `fixpp_*` set == golden AND **0 C++ symbols leak** (SC-003). `fixpp_capi.map` (`fixpp_*; local: *`) unchanged.
- **Version**: `FIXPP_C_ABI_VERSION_MINOR` 2 → 3 (`version.h` + umbrella `c_api.h`); `fixpp_version()` reflects 0.3.0 (FR-020).
- **Codex Gate A** mandatory on every `c_api.h`/`error.h`/`error.cpp` change (`[const §X.6]`).
- The decimal boundary PoD `fixpp_decimal_t` is **untouched** (Article X §3).
