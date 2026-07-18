# Quickstart: fixpp#201 required-presence scoping — validation guide

How to prove the feature end-to-end. Assumes the library submodule cwd and the standard local build (clang-debug, `-j2` per the WSL2 build-resource cap).

## Prerequisites

- Configured build dir (bypass the conan preset collision if needed):
  `cmake -S . -B build/debug -DCMAKE_TOOLCHAIN_FILE=<conan-toolchain> -DCMAKE_BUILD_TYPE=Debug -G Ninja`
- For the QuickFIX parity golden **regeneration only**: quickfix-cpp 1.16.0 at the parent `reference-engines/` and `-DFIXPP_BUILD_QUICKFIX_GOLDEN=ON`. CI and normal runs consume the checked-in golden and need neither.

## 1. Group-scope fix (US1) — conforming message accepted

US1's acceptance criterion is that a **conforming frame is accepted by the runtime `validate()`** — so this step runs the **real-frame accept regression** (Contract 4), not a set-membership check: parse a FIX44 PositionReport (AP) without NoUnderlyings (and a FIX42 conforming frame) and pass each through `dictionary_driven_validator::validate()`; expect **ACCEPT** (was: `wire_required_field_missing(732)`). **The FIX50SP2 TradeCaptureReport (AE) leg is verified at the derivation tier (§1a), NOT full-frame `validate()`** — FIX50SPx ship an empty `<header/>` (FIXT app dict), so `validate()` rejects on tag 8 before the required scan (L-041-2 / #203); FIX44 carries the end-to-end frame accept. This is a grouped-bucket pin in `wire_pure_tests` (LABELS `"079;wire"`, T024) — select via `ctest -L wire` (Article VII §8: by label, not exe-name). See [contracts Contract 4](./contracts/census-and-agreement.md).

## 1a. Required-set derivation unit check (supplementary)

`required_scope_test.cpp` compiles into the grouped `dictionary_pure_tests` binary (registered only as `add_test(NAME dictionary_pure_tests)`), so it is selected by its **bucket label**, not by exe-name regex (Article VII §8 — `-L`, never `-R <file>`, which would match no test name and run nothing):
```
ctest --test-dir build/debug -L dictionary -V
```
Expect (**dictionary-derivation only — does NOT exercise `validate()`**, so it corroborates but does not by itself prove US1): FIX44 PositionReport (AP) required set excludes 732/733; FIX50SP2 TradeCaptureReport (AE) excludes 54; control FIX44 NewOrderSingle (D) excludes Symbol(55). (`-L dictionary` runs the whole dictionary bucket; the derivation cases above are among them.)

## 2. Per-instance group required check (US2)

`validator_type_check_test.cpp` compiles into the grouped `wire_pure_tests` binary, which T024 labelled `"079;wire"` (`tests/wire/CMakeLists.txt`), so select it by label (Article VII §8), not by exe-name regex:
```
ctest --test-dir build/debug -L wire -V
```
Expect: a group instance missing a required member → rejected with the offending tag; complete instances accepted.

## 3. Non-circular census (US4) — the correctness proof

The census is a standalone **exact-set completeness gate**, so it is selected live by `-R` (Article VII §8 explicitly carves these out from the `-L` grouped-bucket rule):
```
ctest --test-dir build/debug -R required_scope_census -V
```
Expect: exact set-equality across all 10 dicts — the message-level set (runtime Step-2 probe surface + IR **data-structure** projection) **and** the per-group required-member set (Contract 1a), plus the shipped max per-group required-member count. To confirm it is not a tautology, prove **two** RED witnesses ([contracts Contract 1](./contracts/census-and-agreement.md)): (a) revert the `in_group` gate in `expand_field_list` → census RED; (b) inject a synthetic optional-component-`required='Y'` field → the stronger full-component-AND oracle drops it while the loader keeps it → census RED. **⚠️ SUPERSEDED (2026-07-19, T020 fix):** "even though the real corpus has 0 optional-component sites" is now false — the real corpus had 4 genuine sites (T015 census finding), fixed in `xml_loader.cpp`'s `component_required` threading; witness (b)'s synthetic proof still independently demonstrates the mechanism, it is just no longer the sole non-trivial RED source for this leg.

## 4. QuickFIX parity (US4) — the tiebreaker (9 dicts)

Also a standalone exact-set gate, selected live by `-R` (Article VII §8 carve-out). Consume the checked-in golden (no QuickFIX link):
```
ctest --test-dir build/debug -R required_scope_parity -V
```
The golden is captured from QuickFIX via `DataDictionary::isRequiredField(msgType, tag)` (encodes the component AND-rule + per-group required members `:560/:570`), with a manifest/hash + stale-golden regen rule (075 precedent). To regenerate after a dictionary change (local only):
```
cmake -S . -B build/qfgolden -DFIXPP_BUILD_QUICKFIX_GOLDEN=ON <...> && cmake --build build/qfgolden --target quickfix_required_golden -j2
```
Expect: QuickFIX `DataDictionary` required set == census oracle, per message, 9 QuickFIX dicts (no vlatest row — the genuine optional-component blind spot is vlatest-only, guarded by step 3's stronger walker + synthetic RED witness).

## 5. Two-tier agreement (US3)

The two-tier verdict-agreement test (`required_scope_two_tier_test`, LABELS `"079;wire;two-tier"`) is ordinary isolation-safe verdict-comparison coverage, selected by label (Article VII §8):
```
ctest --test-dir build/debug -L two-tier -V
```
Expect: runtime and generated typed `validate_<Msg>` agree — confirming no codegen change was needed. **Scope: v44 / v50sp2 / vlatest**. **v44** carries the end-to-end full-frame verdict comparison; **v50sp2 / vlatest** are compared at the **derivation tier** (both tiers' message-level required set excludes the group tag) because standalone full-frame `validate()` is blocked by the empty FIXT header (L-041-2 / #203, per Contract 3). FIX42 has no typed validator (L-077-1/#196; `main.cpp:132`), covered runtime-only (steps 1/2) plus census-vs-IR-structure (step 3).

## 6. Regression floor (no read-golden / ABI drift)

These are whole-binary grouped buckets, so select them by **label** (Article VII §8 — `ctest -L`, never `-R <exe-name>`). The real shipped `LABELS` values are `codegen` (carries `codegen_determinism_test`), `dictionary` (carries `dictionary_pure_tests`), and `wire` (carries `wire_dict_tests`); `wire_pure_tests` currently carries **no** label — /tasks (T024) attaches a `wire` (or feature) label so it is selected here too:
```
ctest --test-dir build/debug -L 'codegen|dictionary|wire' -j2
```
Expect: all green **and a non-zero test count selected** (a mis-typed `-L` regex matches nothing and ctest still exits 0 — a silent false-green; assert `>0` tests ran); v44/v42/vt11/v50sp2/vlatest read goldens byte-identical; no C-ABI change. (Bucket labels follow the 068 grouping precedent — set in each module's CMakeLists; `wire_pure_tests` is labelled `"079;wire"` by T024.)

## Success = all steps green, with step 3's census proven RED on BOTH witnesses (the `in_group` revert AND the synthetic optional-component injection), and the same-PR perf bench reported.
