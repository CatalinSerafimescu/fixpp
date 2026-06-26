# API Surface Checklist: Thin End-to-End Python Binding (PY-001)

**Purpose**: Requirements-quality gate for the thin `fixpp.*` Python module surface (selective wrap, naming convention, out-param typemaps, error bridge). Audience: Gate B reviewer.
**Created**: 2026-06-26
**Feature**: [spec.md](../spec.md) · contracts: [python-module-surface.md](../contracts/python-module-surface.md), [swig-typemap-contract.md](../contracts/swig-typemap-contract.md)

## Requirement Completeness

- [x] CHK001 Is the exact set of in-scope wrapped functions enumerated as a closed list (not "etc.")? [Completeness, contracts/python-module-surface.md] — PASS: `contracts/python-module-surface.md` contains a closed table (Python call / Wraps / Returns row per function) terminated by a named "Out of scope" section; no "etc." open-ended language.
- [x] CHK002 Is an out-param→Python-return-value typemap requirement specified for **every** `**out` handle and scalar out-param (dict/engine/engine_config/session/session_config/msg, `bool*`, `uint16_t*`)? [Completeness, Spec §FR-001/§FR-002/§FR-004, contract T-2] — PASS: `swig-typemap-contract.md T-2` enumerates all six `**out` handle types (dict, engine, engine_config, session, session_config, msg) plus `bool* out_established` → Python bool and `uint16_t* port_out` → Python int; FR-001/FR-002/FR-004 each cite T-2.
- [x] CHK003 Is the error model specified for every non-OK return — a single `fixpp.Error` carrying the `fixpp_strerror` text — with no silent-swallow path left undefined? [Completeness, Spec §FR-008] — PASS: FR-008 mandates "a raised generic `fixpp.Error` carrying the `fixpp_strerror` text"; `python-module-surface.md §Error model` repeats this; `swig-typemap-contract.md T-5` specifies the `%exception` pattern and explicitly names the only exceptions (poll functions that return a value); no silent-swallow path exists.
- [x] CHK004 Are the functions that legitimately return a value instead of raising (`session_is_established`, `session_acceptor_bound_endpoint`) explicitly enumerated as exceptions to the raise-on-non-OK rule? [Completeness, contract T-5] — PASS: `swig-typemap-contract.md T-5` explicitly states "Poll functions that legitimately return a value with `FIXPP_ERR_OK` (`is_established`, `acceptor_bound_endpoint`) return that value, not raise" and names both functions; `python-module-surface.md` table shows these returning `bool` / `int` respectively.
- [x] CHK005 Is the version-string surface (`version_string`) specified as part of the in-scope surface? [Completeness, Spec §FR-009] — PASS: `python-module-surface.md` table includes `fixpp.version_string()` → wraps `fixpp_version_string` → returns `str`; FR-009 mandates it; tasks T001 makes it the baseline smoke-test function.
- [x] CHK006 Is the out-of-scope surface (group accessors, typed getters, `register_send_callback`, `msg_clone`, field iteration) explicitly named and deferred, so the wrap boundary is unambiguous? [Coverage, contracts/python-module-surface.md §Out of scope] — PASS: `python-module-surface.md §Out of scope (named, deferred)` explicitly lists repeating-group accessors, int/double/decimal field getters/setters, `register_send_callback` (toApp), `msg_clone`, `field_count`/`field_at` iteration, typed exceptions (PY-003), full GIL discipline (PY-002), lifetime hardening (PY-004), and wheel/pip/abi3 (PY-005).

## Requirement Clarity

- [x] CHK007 Is the `%rename` prefix-strip rule stated unambiguously — which prefixes (`fixpp_` for functions, `FIXPP_` for enum constants) are stripped, and that it applies to functions AND enum constants? [Clarity, tasks.md T005] — PASS: `tasks.md T005` explicitly names both prefixes (`fixpp_` for functions, `FIXPP_` for enum constants) and states the rule applies to "wrapped functions and enum constants"; the example `fixpp.fixpp_version_string()` → `fixpp.version_string()` makes the transformation unambiguous.
- [x] CHK008 Is the `bytes`-vs-`str` distinction specified for each typemapped param — `bytes` for the committed wire payload and `session_send` frame, `str` (UTF-8) for NUL-terminated config inputs? [Clarity, contract T-3] — PASS: `swig-typemap-contract.md T-3` states "`bytes` is the correct Python type for the committed wire payload (binary, SOH-delimited) — not `str`. The config `const char*` inputs above are the opposite case: NUL-terminated C strings, so `str` (UTF-8) is correct."
- [x] CHK009 Is the `engine_create(cfg)` hand-wrapper requirement specified precisely (Python passes only `cfg`; the wrapper injects `FIXPP_C_ABI_VERSION_MAJOR/_MINOR` into the real 4-arg call)? [Clarity, contract T-1, Spec §FR-002] — PASS: `swig-typemap-contract.md T-1` states "provide a thin `engine_create(cfg)` hand-wrapper that calls the real 4-arg `fixpp_engine_create(cfg, FIXPP_C_ABI_VERSION_MAJOR, FIXPP_C_ABI_VERSION_MINOR, &out)` (`engine.h:81-84`). Python callers pass only `cfg`; the wrapper injects the version macros." Confirmed against `engine.h:81-84` and `research.md D-4`.

## Requirement Consistency

- [x] CHK010 Are the Python-facing names in the contracts (`fixpp.session_open`, `fixpp.ROLE_ACCEPTOR`, `fixpp.version_string`) consistent with the `%rename` rule in T005 and the baseline name in T001 (`version_string`)? [Consistency, contracts/python-module-surface.md, tasks.md T001/T005] — PASS: every name in `python-module-surface.md` (e.g. `fixpp.session_open`, `fixpp.ROLE_ACCEPTOR`, `fixpp.version_string`) is the result of applying the T005 `%rename` rule (strip `fixpp_` / `FIXPP_` prefix) to the underlying C-ABI symbol; T001's smoke-test name `version_string` is consistent with T005's example.
- [x] CHK011 Is the existing `test_smoke.py` call reconciled to the post-`%rename` name (`fixpp.version_string()`), so the kept smoke test is consistent with the renamed surface? [Consistency, tasks.md T005] — PASS: `tasks.md T005` explicitly records the before/after: "This renames the existing `test_smoke.py` call `fixpp.fixpp_version_string()` → `fixpp.version_string()` — update it (T001's baseline already says `version_string`)." The reconciliation is an explicit stated obligation, not an implied side-effect.
- [x] CHK012 Does the error-model description in the contract (`fixpp.Error` + strerror) agree with §FR-008 and with T012's `%exception`-scoped-to-`fixpp_error_t`-returning-functions requirement (not blanket)? [Consistency, Spec §FR-008, tasks.md T012] — PASS: FR-008, `python-module-surface.md §Error model`, `swig-typemap-contract.md T-5`, and `tasks.md T012` all agree: `%exception` (or per-call check) is scoped to `fixpp_error_t`-returning functions; poll functions (`is_established`, `acceptor_bound_endpoint`) return their value; no blanket exception wrapping; consistent across all four artifacts.

## Acceptance Criteria Quality

- [x] CHK013 Is the "module importable / round-trip completable with no C/C++ toolchain or separate fixpp shared library present" criterion objectively verifiable? [Measurability, Spec §SC-001] — PASS: SC-001 states the criterion and the mechanism that makes it verifiable is the T003 static-link requirement (no `.so` runtime dependency) + the Tier-1 `python-bindings` CI job running the pytest in a standard environment; pass/fail is binary.
- [x] CHK014 Is "the field value read in the callback equals the value sent" stated as an exact-equality acceptance, not "a message arrived"? [Measurability, Spec §SC-003] — PASS: SC-003 states "The field value read in the callback equals the value sent by the initiator (a correct round-trip, not merely 'a message arrived')"; the parenthetical explicitly rules out the weaker "message arrived" proxy.

## Edge Case Coverage

- [x] CHK015 Is embedded-NUL rejection specified for the NUL-terminated config-string inputs (so a `str` with an interior NUL fails rather than silently truncating the C string)? [Edge Case, contract T-3] — PASS: `swig-typemap-contract.md T-3` states "reject an embedded NUL (these are NUL-terminated C inputs, not ptr+len)"; the embedded-NUL case is an explicit, named obligation, not an implied behavior.
- [x] CHK016 Is the `cert`/`key` `None`→`NULL` mapping specified for `set_security` under the plaintext kind? [Edge Case, contract T-3, Spec §FR-004a] — PASS: `swig-typemap-contract.md T-3` states "`cert`/`key` accept `None` → `NULL` (ignored for the plaintext kind)"; `python-module-surface.md` `set_security` row notes "None→NULL for plaintext".

## Ambiguities & Assumptions

- [x] CHK017 Is the deferral of exact enum constant names ("taken from `session.h` at implement time") an acceptable, bounded ambiguity, or does it need pinning before implement? [Ambiguity, contracts/python-module-surface.md] — PASS: the critical constants are already confirmed in the shipped `session.h` (`FIXPP_SECURITY_INSECURE_PLAIN_TCP=1`, `FIXPP_RESET_SEQNUM_BILATERAL_LENIENT=1`, `FIXPP_ROLE_ACCEPTOR`/`ROLE_INITIATOR`) and documented in `research.md D-4`; the `%rename` rule deterministically strips `FIXPP_` leaving the short names; the deferral is bounded to a single header lookup at implement time, not an open design question.
- [x] CHK018 Is the assumption that SWIG generates flat function wrappers (no Pythonic classes/context managers in PY-001) documented as an explicit scope boundary? [Assumption, Spec §Assumptions] — PASS: `spec.md §Assumptions` states "this binding is a thin, flat layer over the C-ABI. Pythonic ergonomics (classes, context managers) are a later, separate concern, not this feature"; the scope boundary is explicit.

## Audit Result

| Disposition | Count |
|---|---|
| PASS | 18 |
| SPEC-FIXED | 0 |
| DD-DECIDED | 0 |
| WAIVED | 0 |
| **Total** | **18** |

### SPEC-FIXED items
None.

### DD-DECIDED items
None.

### WAIVED items
None.

Anchors spot-verified: `[2m §4.1]`, `[2m §4.2]`, `[2m §5]`, `[2m §6.1]`, `[2m §6.5]`, `[2m §7]`, `[const §IV.3]`, `[const §VII.2]`, `[const §VII.3]`, `[const §VII.4]`, `[const §IX.2]`, `[const §XII.5]` — all resolve in signed-off revision `.specify/2m-pybind.md` (Draft v0.3 Gate A r2) and `.specify/constitution.md`.
