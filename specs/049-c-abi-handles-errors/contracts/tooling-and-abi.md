# Contract — Audit tooling + ABI gate delta (CA-002/CA-003 enforcement, FR-012/013/019)

## `tools/abi_history/error_codes_v1.txt` (NEW, append-only)
- One line per published code: `<numeric>\t<SYMBOL>\t<introducing_minor>`, where `introducing_minor` is a **`uint16_t` ABI minor ordinal** (the column `translate_for_consumer(code, uint16_t consumer_minor)` consumes).
- Born complete (clarify decision: full enum now) — every E-2 code present, `introducing_minor` = **2** for every current code (the minor of C-ABI 0.2.0; `[const §X.4]` keys downgrade on the consumer's published ABI minor, stability binds only at MAJOR==1).
- **CI rule**: every `#define FIXPP_ERR_*` in `error.h` appears here with an unchanged numeric→symbol binding; a re-defined slot fails CI.

## `tools/check_capi_occupancy.sh` (NEW, Tier 1)
- The occupancy gate is **two independent checks** — the two quantities are **not** comparable to each other (`[2i §4.3]` ~line 608 + P2-5; comparing 2 decimal `#define`s against 4 decimal variants would always FAIL):
  - **Check A** — header `#define` *layout* equals the `[2i §4.3]` published `#define` values (e.g. decimal publishes exactly 800/801; `decimal_buffer_too_small` reuses cross-cutting `BUFFER_TOO_SMALL`=6).
  - **Check B** — the **source-domain variant-row counts** (`[2i §4.3]` counts `| `*_*` | ` rows in each sibling `[2X §6.X]` errors table: decimal 4, wire 13, dict 20, threading 9, store 10, sync 4, TLS 15, transport 22) equal an expected coalescing-coverage table.
- Asserts `error_codes_v1.txt` has no slot redefinition vs the header.
- Reentrancy-class completeness is a **separate, discrete check** (not bundled here — see below).
- Non-zero exit fails the build. Mirrors the existing shell-gate style used elsewhere in `tools/`.

## `tools/check_capi_reentrancy.sh` — reentrancy-completeness gate (NEW, discrete — FR-014/SC-005)
- A separate, independently-runnable check (NOT folded into `check_capi_occupancy.sh`): for every exported `fixpp_*` declaration in `include/fix/c_api/*.h`, assert the symbol's **doc-block** (the contiguous comment block immediately preceding the declaration) contains **exactly one** of `thread-safe`/`single-thread`/`requires-session-lock` — 0 unannotated, 0 double-classed. Presence "anywhere in the file" is insufficient; it must be the symbol's own doc-block.
- Non-zero exit fails the build (Tier 1).

## `abi-golden` delta (FR-019)
- The gate is an **nm symbol-set check** (not abidiff — see research D-6). Add to `tests/abi/golden/fixpp_capi_symbols.txt`: `fixpp_strerror`, `fixpp_version`, `fixpp_library_version` (keep sorted).
- The decimal renumber is invisible here (`#define`s, not symbols) — its safety net is `error_codes_v1.txt` + the enumerating test.
- abidiff layout-gate upgrade = deferred CA-workstream item (research D-6), NOT this feature.

## Wiring
- Add both tools to the Tier-1 job that runs the other `tools/*.sh` gates (same place `check_layers.py` / corpus gates run). `/speckit-verify` confirms they fire.
