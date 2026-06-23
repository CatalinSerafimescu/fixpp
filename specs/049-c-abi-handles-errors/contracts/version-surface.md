# Contract — Version surface (CA-004)

**Header**: `include/fix/c_api/version.h` (NEW). **Impl**: `src/capi/version.cpp` (NEW).

## Published surface (C-clean) — `[2i §4.5]`
- Macros: `FIXPP_C_ABI_VERSION_MAJOR` / `_MINOR` / `_PATCH` + composite `FIXPP_C_ABI_VERSION` (`(MAJOR<<16)|(MINOR<<8)|PATCH`).
  - **Values this feature**: MAJOR `0`, MINOR `1`→`2`, PATCH `0`. (Stay 0.x — clarify decision; GA does 0→1.)
- `typedef struct fixpp_version { uint16_t major, minor, patch, _reserved; } fixpp_version_t;` (PoD).
- `FIXPP_API_EXPORT fixpp_version_t fixpp_version(void);` — the C-ABI surface version (the macros above, at engine build time).
- `FIXPP_API_EXPORT fixpp_version_t fixpp_library_version(void);` — the C++ library version (`fixpp::core::FIXPP_VERSION`), separate track per `[arch §9.2]`.

## Contract
- Both accessors: **zero allocation**, **thread-safe**, value-typed return.
- `fixpp_version()` MUST equal the compile-time macros for a consumer linked against the same binary.
- The two tracks are independent: a consumer compares macro-vs-runtime on the C-ABI track only for compatibility.

## Version-binding / downgrade rule (the *rule*, not the binding)
- Per `[2i §4.5]`: at `fixpp_engine_create(consumer_major, consumer_minor)` (Feature B) the engine: major≠engine → refuse + `VERSION_MISMATCH`(5); major== & consumer_minor<engine_minor → construct + record `consumer_minor` for the FR-009 downgrade; consumer_minor>engine_minor → construct + info log.
- **Feature A** implements only `translate_for_consumer(...)` (the downgrade math) + reserves `VERSION_MISMATCH`. The engine-create recording is **L-049-1** (Feature B).

## Acceptance (SC-001/SC-006)
- Pure-C program links only the C ABI, calls both accessors, compares to macros.
- Downgrade unit tests per error-surface contract.
