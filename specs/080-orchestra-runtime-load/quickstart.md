# Quickstart / Validation: Orchestra runtime dictionary load

Runnable validation scenarios that prove the feature end-to-end. Author each as a RED-first test (fails before the change, passes after). Build/run on `linux-clang-debug`; group into whole-binary test targets selected by `ctest -L` (Article VII §8).

## Prerequisites

- `dictionaries/orchestra/OrchestraFIXLatest.xml` (074 supply-chain artifact) — the Orchestra fixture.
- An existing classic dictionary (e.g. `dictionaries/quickfix/FIX44.xml`) — the regression baseline.
- FIX-Latest message fixtures reused from 074/076 (no new goldens — spec Assumptions).

## Scenario 1 — C-API loads an Orchestra dictionary (US1 / SC-001)

1. Call `fixpp_dict_load_from_xml("…/OrchestraFIXLatest.xml", &h)`.
2. **Expect** `FIXPP_ERR_OK` and non-null `h` (RED before: `FIXPP_ERR_CAPI_CONFIG_INVALID`).
3. Parse/validate a FIX-Latest message through `h`; **expect** the outcome to equal the same message processed by a `Dictionary` obtained from `dict::OrchestraLoader{}.load(...)` directly (0 divergences).

## Scenario 2 — C-API classic load unchanged (US1 / SC-003, regression)

1. `fixpp_dict_load_from_xml("…/FIX44.xml", &h)` → **expect** `FIXPP_ERR_OK`; dictionary byte-identical to the pre-080 `XmlLoader` result across the existing dictionary-load regression suite.

## Scenario 3 — TOML `dictionary.path` loads Orchestra (US2 / SC-002)

1. Resolve a config with `[dictionary] kind = "path"`, `path = "…/OrchestraFIXLatest.xml"`.
2. **Expect** a successful load; validate a FIX-Latest message through the resolved dictionary (RED before: config error).
3. Classic `dictionary.path` (FIX44) → unchanged (regression).

## Scenario 4 — Dual FIX50SP2 + FIX-Latest config fails cleanly, no abort (US3 / SC-004)

1. Resolve a config naming **both** a FIX50SP2 dictionary and `OrchestraFIXLatest.xml`.
2. **Expect** `LoadResult` to carry a `LoadDiagnostic` with `reason == reason_class::conflicting_dictionaries`, and the process to survive (no `std::abort`). Before 080 this path `std::abort`s at `version_registry` construction — assert via a death-test-style guard that the config-layer path returns the diagnostic instead.
3. Single FIX-Latest-only config → loads successfully (FR-008; the pre-check does not false-trigger).

## Scenario 5 — Dispatch fail-closed on unrecognized root (FR-003 / D-2)

1. `dict::load_any` on an XML file whose root is neither `fix` nor `fixr:repository` → **expect** a `dict::` parse error (not `std::runtime_error`, not a wrong `Dictionary`).

## Scenario 6 — 074 T022h invariant preserved (FR-009, pin)

1. `dict::XmlLoader{}.load("…/OrchestraFIXLatest.xml", mr)` → **expect** it still throws `dict::xml_parse_error` ("root element is not `<fix>`"). The loader-unit reject is unchanged; only the dispatch layer became Orchestra-aware.

## ABI / hygiene gate (SC-005) — `/speckit-verify`

- `nm` exported-symbol golden `tests/abi/golden/fixpp_capi_symbols.txt` — **green without regeneration**.
- Header byte-freeze `tools/check_capi_freeze.sh` / `tools/capi_freeze.sha256` — **green** (`error.h`/`version.h` untouched).
- `FIXPP_C_ABI_VERSION` == `1.5.0`.
- Sanitizer matrix (ASan/UBSan/TSan) over the new tests; coverage on `load_any.cpp` + the pre-check.

## Done when

All six scenarios pass, the ABI gate is green without regeneration, and `/speckit-verify` produces a non-RED decision record for the feature.
