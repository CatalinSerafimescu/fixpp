# Contract — Error block + downgrade go-live + ABI gates

## FIXPP_ERR_SESSION_* block (discharges L-049-2 for reachable variants)

- Append the new published codes (data-model E-4) to `include/fix/c_api/error.h` at their reserved `[2i §4.3]` session/app blocks; **never re-use or re-number an existing slot** (Article X §4; stability not yet binding at major 0 but the audit gate forbids slot re-use regardless).
- Re-point `src/capi/error.cpp` `translate()` arms for the **reachable** variants (E-4) off `FIXPP_ERR_UNKNOWN` to the new codes. `translate()` stays a **total switch, no `default`** (`-Wswitch`). Mutation requirement: each re-pointed arm maps to its **specific** code (flipping one → RED), per [[feedback_completeness_gate_exact_set_not_subset]].
- Non-reachable `session_*`/`app_*`/`log_*`/`otel_*`/`out_of_memory` stay `UNKNOWN` (residual L-049-2, documented).
- `fixpp_strerror` gains a non-null static string for each new code (zero-alloc, FR per A).

## Downgrade go-live (discharges L-049-1)

- `error_codes_v1.txt`: append each new code → symbol + **introducing_minor = 3**. The occupancy gate (`check_capi_occupancy.sh`) must stay green (Check A header layout == `[2i §4.3]`; Check B sibling variant-row counts).
- `fixpp_engine_create` records `consumer_minor`; every fallible C-ABI return runs `translate_for_consumer(code, consumer_minor)` (Feature A's pure fn). SC-004: a `consumer_minor < 3` downgrades a new session code → `FIXPP_ERR_UNKNOWN`; `consumer_minor >= 3` sees the real code. `consumer_major != 0` → `FIXPP_ERR_VERSION_MISMATCH` at create.

## ABI gates (FR-018)

- **nm symbol-golden** (`tests/abi/golden/fixpp_capi_symbols.txt`): append every new exported symbol (engine create/start/destroy; session open/close/send/register_callback/is_established; the two config-builder families). The per-PR nm gate verifies the exported `fixpp_*` set == golden AND **0 C++ symbols leak** (SC-003). `fixpp_capi.map` (`fixpp_*; local: *`) unchanged.
- **Version**: `FIXPP_C_ABI_VERSION_MINOR` 2 → 3 (`version.h` + umbrella `c_api.h`); `fixpp_version()` reflects 0.3.0 (FR-020).
- **Codex Gate A** mandatory on every `c_api.h`/`error.h`/`error.cpp` change (`[const §X.6]`).
- The decimal boundary PoD `fixpp_decimal_t` is **untouched** (Article X §3).
