# Quickstart / Validation: Orchestra runtime dictionary load

Runnable validation scenarios that prove the feature end-to-end. Author each as a RED-first test (fails before the change, passes after). Build/run on `linux-clang-debug`; group into whole-binary test targets selected by `ctest -L` (Article VII §8).

## Prerequisites

- `dictionaries/orchestra/OrchestraFIXLatest.xml` (074 supply-chain artifact) — the Orchestra fixture.
- An existing classic dictionary (e.g. `dictionaries/FIX44.xml`) — the regression baseline.
- FIX-Latest message fixtures reused from 074/076 (no new goldens — spec Assumptions).

## Scenario 1 — C-API loads an Orchestra dictionary (US1 / SC-001)

1. Call `fixpp_dict_load_from_xml("…/OrchestraFIXLatest.xml", &h)`.
2. **Expect** `FIXPP_ERR_OK` and non-null `h` (RED before: `FIXPP_ERR_CAPI_CONFIG_INVALID`).
3. Parse/validate a FIX-Latest message through `h`; **expect** the outcome to equal the same message processed by a `Dictionary` obtained from `dict::OrchestraLoader{}.load(...)` directly (0 divergences).

## Scenario 2 — C-API classic load unchanged (US1 / SC-003, regression)

1. `fixpp_dict_load_from_xml("…/FIX44.xml", &h)` → **expect** `FIXPP_ERR_OK`; dictionary byte-identical to the pre-080 `XmlLoader` result across the existing dictionary-load regression suite.

## Scenario 3 — TOML `dictionary.path` loads Orchestra (US2 / SC-002, FR-008)

1. Resolve a config with a single `[dictionary] kind = "path"`, `path = "…/OrchestraFIXLatest.xml"`.
2. **Expect** a successful load; validate a FIX-Latest message through the resolved dictionary (RED before: config error). This single FIX-Latest-only config loading is FR-008.
3. Classic `dictionary.path` (FIX44) → unchanged (regression).

## Scenario 4 — Dispatch fail-closed on unrecognized root (FR-003 / D-2)

1. `dict::load_any` on an XML file whose root is neither `fix` nor `fixr:repository` → **expect** a `dict::` parse error (not `std::runtime_error`, not a wrong `Dictionary`).

## Scenario 5 — 074 T022h invariant preserved (FR-009, pin)

1. `dict::XmlLoader{}.load("…/OrchestraFIXLatest.xml", mr)` → **expect** it still throws `dict::xml_parse_error` (assert on the exception **type only**, message-agnostic — the shipped 074 test `orchestra_loader_test.cpp` `VendoredOrchestraFileFedToXmlLoaderThrows` already asserts on the type, not the message). The loader-unit reject is unchanged; only the dispatch layer became Orchestra-aware.

## Scenario 6 — FR-004 single shared dispatch (source-inspection gate)

A lightweight source-inspection assertion (not a runtime test) pinning FR-004's "dispatch rule defined once, not duplicated":

1. Assert that `src/capi/dictionary.cpp` (`fixpp_dict_load_from_xml`) calls `dict::load_any` and does **not** inline its own root-element sniff/dispatch.
2. Assert that `src/config/selector_resolver.cpp` (the `dictionary.path` resolver) calls `dict::load_any` and does **not** inline its own root-element sniff/dispatch.
3. **Expect** exactly one root-sniff implementation (`dict::load_any` in `src/dictionary/load_any.cpp`); both surfaces share it. This gives FR-004 a named validation, not just the two behavior tests (Scenarios 1/3, which would still pass if each call site sniffed independently).

## ABI / hygiene gate (SC-005) — `/speckit-verify`

- `nm` exported-symbol golden `tests/abi/golden/fixpp_capi_symbols.txt` — **green without regeneration**.
- Header byte-freeze `tools/check_capi_freeze.sh` / `tools/capi_freeze.sha256` — **green** (`error.h`/`version.h`/`include/fix/c_api/dict.h` untouched).
- `FIXPP_C_ABI_VERSION` == `1.5.0`.
- Sanitizer matrix (ASan/UBSan/TSan) over the new tests; coverage on `load_any.cpp`.

## Close-out: frozen-header docs obligation (PINNED — must not be skipped)

`fixpp_dict_load_from_xml` now also accepts an Orchestra `<fixr:repository>` root, but its declaring header `include/fix/c_api/dict.h` is **byte-frozen** (line 3 of `tools/capi_freeze.sha256`) and its Doxygen prose still says "FIX XML data dictionary / wraps `XmlLoader`". That prose is **retained verbatim for ABI stability** — it MUST NOT be edited. Instead, at close-out add a **non-frozen** public docs / API-reference note (release notes / doc site / behaviors-and-limitations) stating that the entry point accepts both `<fix>` and `<fixr:repository>` roots, and that the frozen `dict.h` wording is deliberately unchanged for ABI stability. This obligation is pinned here so it cannot be skipped; it is also referenced from spec.md SC-005 / the Contract & Compatibility Notes.

## Done when

All six scenarios pass (Scenarios 1–5 runtime; Scenario 6 the FR-004 source-inspection gate), the ABI gate is green without regeneration, the pinned frozen-header docs note is added, and `/speckit-verify` produces a non-RED decision record for the feature.
