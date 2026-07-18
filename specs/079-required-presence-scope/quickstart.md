# Quickstart: fixpp#201 required-presence scoping — validation guide

How to prove the feature end-to-end. Assumes the library submodule cwd and the standard local build (clang-debug, `-j2` per the WSL2 build-resource cap).

## Prerequisites

- Configured build dir (bypass the conan preset collision if needed):
  `cmake -S . -B build/debug -DCMAKE_TOOLCHAIN_FILE=<conan-toolchain> -DCMAKE_BUILD_TYPE=Debug -G Ninja`
- For the QuickFIX parity golden **regeneration only**: quickfix-cpp 1.16.0 at the parent `reference-engines/` and `-DFIXPP_BUILD_QUICKFIX_GOLDEN=ON`. CI and normal runs consume the checked-in golden and need neither.

## 1. Group-scope fix (US1) — conforming message accepted

US1's acceptance criterion is that a **conforming frame is accepted by the runtime `validate()`** — so this step runs the **real-frame accept regression** (Contract 4), not a set-membership check: parse a FIX44 PositionReport (AP) without NoUnderlyings and a FIX50SP2 TradeCaptureReport (AE) without NoSides and pass each through `dictionary_driven_validator::validate()`; expect **ACCEPT** (was: `wire_required_field_missing(732)` / tag 54). This is a grouped-bucket pin — its concrete target + `ctest -L <label>` are finalized at /tasks (Article VII §8: select by label, not exe-name). See [contracts Contract 4](./contracts/census-and-agreement.md).

## 1a. Required-set derivation unit check (supplementary)

`required_scope_test.cpp` compiles into the grouped `dictionary_pure_tests` binary (registered only as `add_test(NAME dictionary_pure_tests)`), so it is selected by its **bucket label**, not by exe-name regex (Article VII §8 — `-L`, never `-R <file>`, which would match no test name and run nothing):
```
ctest --test-dir build/debug -L dictionary -V
```
Expect (**dictionary-derivation only — does NOT exercise `validate()`**, so it corroborates but does not by itself prove US1): FIX44 PositionReport (AP) required set excludes 732/733; FIX50SP2 TradeCaptureReport (AE) excludes 54; control FIX44 NewOrderSingle (D) excludes Symbol(55). (`-L dictionary` runs the whole dictionary bucket; the derivation cases above are among them.)

## 2. Per-instance group required check (US2)

`validator_type_check_test.cpp` compiles into the grouped `wire_pure_tests` binary (registered only as `add_test(NAME wire_pure_tests)`), so `-R validator_type_check_test` would match no test name and run nothing. The `wire_pure_tests` bucket currently carries **no** LABELS, so the concrete label (added to the bucket) or a standalone target is **finalized at /tasks**; select it by `ctest -L <label>` (Article VII §8), not by exe-name regex:
```
ctest --test-dir build/debug -L <wire bucket label — finalized at /tasks> -V
```
Expect: a group instance missing a required member → rejected with the offending tag; complete instances accepted.

## 3. Non-circular census (US4) — the correctness proof

The census is a standalone **exact-set completeness gate**, so it is selected live by `-R` (Article VII §8 explicitly carves these out from the `-L` grouped-bucket rule):
```
ctest --test-dir build/debug -R required_scope_census -V
```
Expect: exact set-equality across all 10 dicts — the message-level set (runtime Step-2 probe surface + IR **data-structure** projection) **and** the per-group required-member set (Contract 1a), plus the shipped max per-group required-member count. To confirm it is not a tautology, prove **two** RED witnesses ([contracts Contract 1](./contracts/census-and-agreement.md)): (a) revert the `in_group` gate in `expand_field_list` → census RED; (b) inject a synthetic optional-component-`required='Y'` field → the stronger full-component-AND oracle drops it while the loader keeps it → census RED (even though the real corpus has 0 optional-component sites).

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

The two-tier verdict-agreement test is ordinary isolation-safe verdict-comparison coverage — NOT an Article VII §8 exact-set standalone gate — so its default selection is a grouped bucket by `ctest -L <label>`, not `-R <exe-name>`. The concrete label/target is **finalized at /tasks** (a standalone target only if /tasks explicitly justifies the isolation exception):
```
ctest --test-dir build/debug -L <two-tier bucket label — finalized at /tasks> -V
```
Expect: runtime and generated typed `validate_<Msg>` return identical verdicts for the affected frames — confirming no codegen change was needed. **Scope: v44 / v50sp2 / vlatest only** — FIX42 has no typed validator (L-077-1/#196; `main.cpp:132`), so it is covered runtime-only (steps 1/2) plus census-vs-IR-structure (step 3).

## 6. Regression floor (no read-golden / ABI drift)

These are whole-binary grouped buckets, so select them by **label** (Article VII §8 — `ctest -L`, never `-R <exe-name>`):
```
ctest --test-dir build/debug -L 'codegen_determinism|dictionary_pure|wire_pure|wire_dict' -j2
```
Expect: all green; v44/v42/vt11/v50sp2/vlatest read goldens byte-identical; no C-ABI change. (The exact label strings for these buckets follow the 068 grouping precedent — set in each module's CMakeLists, finalized at /tasks.)

## Success = all steps green, with step 3's census proven RED on BOTH witnesses (the `in_group` revert AND the synthetic optional-component injection), and the same-PR perf bench reported.
