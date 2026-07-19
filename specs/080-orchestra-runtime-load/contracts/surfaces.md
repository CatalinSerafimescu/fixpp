# Contract: widened acquisition surfaces

## S1 — C-API `fixpp_dict_load_from_xml` (behavior widened, ABI unchanged)

`src/capi/dictionary.cpp:48` — signature and symbol unchanged:

```c
fixpp_error_t fixpp_dict_load_from_xml(const char* path, fixpp_dict_t** out_dict);
```

| Input | Before 080 | After 080 |
|---|---|---|
| classic `<fix>` dictionary | `FIXPP_ERR_OK` | `FIXPP_ERR_OK` (unchanged, byte-identical dict) |
| Orchestra `<fixr:repository>` | `FIXPP_ERR_CAPI_CONFIG_INVALID` | **`FIXPP_ERR_OK`** (contract-widening) |
| `null` path/out, malformed, unreadable | existing error | existing error (unchanged) |

- **ABI**: no new/changed symbol, no new `fixpp_error_t` value, no `error.h`/`version.h` edit. `nm` symbol golden + `capi_freeze.sha256` pass without regeneration (SC-005). `FIXPP_C_ABI_VERSION` stays `1.5.0`.
- **Documentation obligation**: the widening (an input that returned `CONFIG_INVALID` now succeeds) MUST be recorded as a `behaviors-and-limitations` note at close-out.
- **Not reachable**: the dual-dictionary collision — `fixpp_dict_load_from_xml` loads a single dictionary and `fixpp_engine_create` never populates a multi-dictionary registry (research D-3), so no C-API error surfaces for it. This is the rationale for the descoped collision-error leg (Gate A round 1), not a live requirement — see the spec behaviors-and-limitations note (spec.md, "Behaviors-and-limitations note").

## S2 — TOML `dictionary.path` (behavior widened)

`src/config/selector_resolver.cpp` (~L359) — `dictionary.path` now accepts an Orchestra document:

| `dictionary.path` target | Before 080 | After 080 |
|---|---|---|
| classic `<fix>` dictionary | loads (via `XmlLoader`) | loads (via `load_any`→`XmlLoader`), unchanged |
| Orchestra `<fixr:repository>` | config error | **loads** (via `load_any`→`OrchestraLoader`) |

## Test obligations (surfaces)

- C-API: Orchestra path → `FIXPP_ERR_OK` + a usable handle whose parse/validate/read matches `OrchestraLoader::load` (SC-001). Classic path → unchanged (SC-003).
- TOML: Orchestra `dictionary.path` → loads + validates FIX-Latest (SC-002). Classic `dictionary.path` → unchanged (SC-003). Single FIX-Latest config → loads (FR-008).
- ABI: `nm` symbol golden + `capi_freeze.sha256` green without regeneration; `FIXPP_C_ABI_VERSION == 1.5.0` (SC-005).
